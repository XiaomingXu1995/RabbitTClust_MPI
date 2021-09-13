#include "SketchInfo.h"
#include "kseq.h"
#include <fstream>
#include <sstream>
#include <zlib.h>

#ifdef THREADPOOL_MINHASH
#include "ThreadPool.h"
#endif

#ifdef RABBIT_IO
#include "FastxStream.h"
#include "FastxChunk.h"
#include "Formater.h"
#include "DataQueue.h"
#include <thread>
#include <cstdint>
#include <unordered_map>
#endif

#include "parameter.h"

KSEQ_INIT(gzFile, gzread);
using namespace std;

bool cmpSketch(SketchInfo s1, SketchInfo s2){
	return s1.id < s2.id;
}

#ifdef THREADPOOL_MINHASH
struct SketchInput{
	SketchInput(SketchInfo sketchInfoNew, string sketchFuncNew, char * seqNew, int indexNew): sketchInfo(sketchInfoNew), sketchFunc(sketchFuncNew), seq(seqNew), index(indexNew){}

	SketchInfo sketchInfo;
	string sketchFunc;
	char * seq;
	int index;

};

struct SketchOutput{
	SketchInfo sketchInfo;
	//int index;
};

SketchOutput* sketchBySequence(SketchInput* input){
	SketchOutput * output = new SketchOutput();
	SketchInfo sketchInfo = input->sketchInfo;
	string sketchFunc = input->sketchFunc;
	if(sketchFunc == "MinHash"){
		sketchInfo.minHash->update(input->seq);
	}
	else if(sketchFunc == "WMH"){
		//wmh->update(ks1->seq.s);
		//wmh->computeHistoSketch();
		sketchInfo.WMinHash->update(input->seq);
		sketchInfo.WMinHash->computeHistoSketch();
	}
	else if(sketchFunc == "HLL"){
		sketchInfo.HLL->update(input->seq);
	}
	else if(sketchFunc == "OMH"){
		sketchInfo.OMH->buildSketch(input->seq);
	}
	free(input->seq);
	sketchInfo.id = input->index;
	output->sketchInfo = sketchInfo;
	return output;
}


void useThreadOutput(SketchOutput * output, vector<SketchInfo> &sketches){
	//SketchInfo tmpSimilarityInf
	sketches.push_back(output->sketchInfo);
}

#endif

#ifdef RABBIT_IO
typedef rabbit::core::TDataQueue<rabbit::fa::FastaChunk> FaChunkQueue;
int producer_fasta_task(std::string file, rabbit::fa::FastaDataPool* fastaPool, FaChunkQueue &dq){
	std::cout << "filename" << file << std::endl;
	rabbit::fa::FastaFileReader* faFileReader;
	faFileReader = new rabbit::fa::FastaFileReader(file, *fastaPool, false);
	int n_chunks = 0;
	while(1){
		rabbit::fa::FastaChunk* faChunk = new rabbit::fa::FastaChunk;
		faChunk = faFileReader->readNextChunkList();
		if(faChunk == NULL) break;
		n_chunks++;
		dq.Push(n_chunks, faChunk);
		//cerr << "reading : " << n_chunks << endl;

	}
	dq.SetCompleted();

	return 0;
}

void consumer_fasta_task(rabbit::fa::FastaDataPool* fastaPool, FaChunkQueue &dq, string sketchFunc, Sketch::WMHParameters * parameters, vector<SketchInfo> *sketches){
	int line_num = 0;
	rabbit::int64 id = 0;

	rabbit::fa::FastaChunk *faChunk;
	while(dq.Pop(id, faChunk)){
		std::vector<Reference> data;
		int ref_num = rabbit::fa::chunkListFormat(*faChunk, data);
		for(Reference &r: data){
			string name = r.name;
			string comment = r.comment;
			int length = r.length;
			SequenceInfo curSeq{name, comment, 0, length};

			SketchInfo tmpSketchInfo; 
			tmpSketchInfo.seqInfo = curSeq;
			if(sketchFunc == "MinHash"){
				Sketch::MinHash * mh1 = new Sketch::MinHash(KMER_SIZE, MINHASH_SKETCH_SIZE);
				mh1->update((char*)r.seq.c_str());
				tmpSketchInfo.minHash = mh1;
			}
			else if(sketchFunc == "WMH"){
				Sketch::WMinHash * wmh = new Sketch::WMinHash(*parameters);
				wmh->update((char*)r.seq.c_str());
				wmh->computeHistoSketch();
				tmpSketchInfo.WMinHash = wmh;
			}
			else if(sketchFunc == "HLL"){
				Sketch::HyperLogLog * hll = new Sketch::HyperLogLog(HLL_SKETCH_BIT);
				hll->update((char*)r.seq.c_str());
				tmpSketchInfo.HLL = hll;
			}
			else if(sketchFunc == "OMH"){
				Sketch::OrderMinHash* omh = new Sketch::OrderMinHash();
				omh->buildSketch((char*)r.seq.c_str());
				tmpSketchInfo.OMH = omh;
			}

			//tmpSketchInfo.minHash = mh1;
			tmpSketchInfo.id = r.gid;
			sketches->push_back(tmpSketchInfo);

		}
		rabbit::fa::FastaDataChunk *tmp = faChunk->chunk;
		do{
			if(tmp != NULL){
			fastaPool->Release(tmp);
			tmp = tmp->next;
			}
		}while(tmp!=NULL);

	}

}


#endif

void getCWS(double * r, double * c, double * b, int sketchSize, int dimension){
	const int DISTRIBUTION_SEED = 1;
	default_random_engine generator(DISTRIBUTION_SEED);
	gamma_distribution<double> gamma(2.0, 1.0);
	uniform_real_distribution<double> uniform(0.0, 1.0);

	for(int i = 0; i < sketchSize * dimension; ++i){
		r[i] = gamma(generator);
		c[i] = log(gamma(generator));
		b[i] = uniform(generator) * r[i];
	}
}

bool sketchSequences(string inputFile, string sketchFunc, vector<SketchInfo>& sketches, int threads){
	gzFile fp1;
	kseq_t * ks1;
	cerr << "input File is: " << inputFile << endl;
	fp1 = gzopen(inputFile.c_str(), "r");
	if(fp1 == NULL){
		fprintf(stderr, "cannot open the genome file, %s\n", inputFile.c_str());
		//printfUsage();
		return false;
	}
	
	ks1 = kseq_init(fp1);
	Sketch::WMHParameters parameters;
	if(sketchFunc == "WMH"){
		parameters.kmerSize = KMER_SIZE;
		parameters.sketchSize = WMH_SKETCH_SIZE;
		parameters.windowSize = WINDOW_SIZE;
		parameters.r = (double *)malloc(parameters.sketchSize * pow(parameters.kmerSize, 4) * sizeof(double));
		parameters.c = (double *)malloc(parameters.sketchSize * pow(parameters.kmerSize, 4) * sizeof(double));
		parameters.b = (double *)malloc(parameters.sketchSize * pow(parameters.kmerSize, 4) * sizeof(double));
		getCWS(parameters.r, parameters.c, parameters.b, parameters.sketchSize, pow(parameters.kmerSize, 4));
	}

	int index = 0;
	#ifdef THREADPOOL_MINHASH
	ThreadPool<SketchInput, SketchOutput> threadPool(0, threads);
	while(1){
		int length = kseq_read(ks1);
		if(length < 0) break;
		if(length < KMER_SIZE) continue;

		string name = ks1->name.s;
		string comment = ks1->comment.s;
		SequenceInfo curSeq{name, comment, 0, length};
		
		char * seqCopy = (char*) malloc((length+1) * sizeof(char));
		memcpy(seqCopy, ks1->seq.s, length+1);
		SketchInfo sketchInfo;
		sketchInfo.seqInfo = curSeq;
		if(sketchFunc == "MinHash"){
			Sketch::MinHash * mh1 = new Sketch::MinHash(KMER_SIZE, MINHASH_SKETCH_SIZE);
			sketchInfo.minHash = mh1;
		}
		else if(sketchFunc == "WMH"){
			Sketch::WMinHash * wmh = new Sketch::WMinHash(parameters);
			sketchInfo.WMinHash = wmh;
		}
		else if(sketchFunc == "HLL"){
			Sketch::HyperLogLog *hll = new Sketch::HyperLogLog(HLL_SKETCH_BIT);
			sketchInfo.HLL = hll;
		}
		else if(sketchFunc == "OMH"){
			Sketch::OrderMinHash *omh = new Sketch::OrderMinHash();
			sketchInfo.OMH = omh;
		}

		threadPool.runWhenThreadAvailable(new SketchInput(sketchInfo, sketchFunc, seqCopy, index), sketchBySequence);

		while(threadPool.outputAvailable()){
			useThreadOutput(threadPool.popOutputWhenAvailable(), sketches);
		}
		index++;

	}//end while
	while(threadPool.running()){
		useThreadOutput(threadPool.popOutputWhenAvailable(), sketches);
	}
	#else 
	#ifdef RABBIT_IO
	int th = threads - 1;//consumer threads number;
	vector<SketchInfo>  sketchesArr[th];
	//vector<SimilarityInfo>  similarityInfosArr[th];
	
	rabbit::fa::FastaDataPool *fastaPool = new rabbit::fa::FastaDataPool(256, 1<< 24);
	FaChunkQueue queue1(128, 1);
	//cout << "--------------------" << endl;
	std::thread producer(producer_fasta_task, inputFile, fastaPool, std::ref(queue1));
	std::thread **threadArr = new std::thread* [th];

	for(int t = 0; t < th; t++){
		threadArr[t] = new std::thread(std::bind(consumer_fasta_task, fastaPool, std::ref(queue1), sketchFunc, &parameters, &sketchesArr[t]));
	}
	producer.join();
	for(int t = 0; t < th; t++){
		threadArr[t]->join();
	}


	for(int i = 0; i < th; i++){
		for(int j = 0; j < sketchesArr[i].size(); j++){
			sketches.push_back(sketchesArr[i][j]);
			//similarityInfos.push_back(similarityInfosArr[i][j]);
		}
	}

	cerr << "the size of sketches is: " << sketches.size() << endl;
	//cerr << "the size of similarityInfos is: " << similarityInfos.size() << endl;
//	exit(0);

	#else 
	//for single thread sketch
	cerr << "start read the file " << endl;
	while(1){
		int length = kseq_read(ks1);
		if(length < 0) break;
		if(length < KMER_SIZE) continue;

		string name = ks1->name.s;
		string comment = ks1->comment.s;
		SequenceInfo curSeq{name, comment, 0, length};

		SketchInfo tmpSketchInfo;
		tmpSketchInfo.seqInfo = curSeq;
		
		if(sketchFunc == "MinHash"){
			Sketch::MinHash *mh1 = new Sketch::MinHash(KMER_SIZE, MINHASH_SKETCH_SIZE);
			mh1->update(ks1->seq.s);
			tmpSketchInfo.minHash = mh1;
		}
		else if(sketchFunc == "WMH"){
			Sketch::WMinHash *wmh = new Sketch::WMinHash(parameters);
			wmh->update(ks1->seq.s);
			wmh->computeHistoSketch();
			tmpSketchInfo.WMinHash = wmh;
		}
		else if(sketchFunc == "HLL"){
			Sketch::HyperLogLog* hll = new Sketch::HyperLogLog(HLL_SKETCH_BIT);
			hll->update(ks1->seq.s);
			tmpSketchInfo.HLL = hll;
		}
		else if(sketchFunc == "OMH"){
			Sketch::OrderMinHash* omh = new Sketch::OrderMinHash();
			omh->buildSketch(ks1->seq.s);
			tmpSketchInfo.OMH = omh;
		}

		tmpSketchInfo.id = index;
		sketches.push_back(tmpSketchInfo);
		index++;

	}//end while
	#endif
	#endif
	cerr << "the number of sequence is: " << index << endl;
	gzclose(fp1);
	kseq_destroy(ks1);

	sort(sketches.begin(), sketches.end(), cmpSketch);

	return true;
}

bool sketchFiles(string inputFile, string sketchFunc, vector<SketchInfo>& sketches, int threads){
	fprintf(stderr, "input fileList, sketch by file\n");
	fstream fs(inputFile);
	if(!fs){
		fprintf(stderr, "error open the inputFile: %s\n", inputFile.c_str());
		return false;
	}
	vector<string> fileList;
	string fileName;
	while(getline(fs, fileName)){
		fileList.push_back(fileName);
	}

	Sketch::WMHParameters parameter;
	if(sketchFunc == "WMH"){
		parameter.kmerSize = KMER_SIZE;
		parameter.sketchSize = WMH_SKETCH_SIZE;
		parameter.windowSize = WINDOW_SIZE;
		parameter.r = (double *)malloc(parameter.sketchSize * pow(parameter.kmerSize, 4) * sizeof(double));
		parameter.c = (double *)malloc(parameter.sketchSize * pow(parameter.kmerSize, 4) * sizeof(double));
		parameter.b = (double *)malloc(parameter.sketchSize * pow(parameter.kmerSize, 4) * sizeof(double));
		getCWS(parameter.r, parameter.c, parameter.b, parameter.sketchSize, pow(parameter.kmerSize, 4));
	}

	#pragma omp parallel for num_threads(threads) schedule(dynamic)
	for(int i = 0; i < fileList.size(); i++){
		//cerr << "start the file: " << fileList[i] << endl;
		gzFile fp1;
		kseq_t* ks1;
		fp1 = gzopen(fileList[i].c_str(), "r");
		if(fp1 == NULL){
			fprintf(stderr, "cannot open the genome file: %s\n", fileList[i].c_str());
			exit(1);
			//return false;
		}
		ks1 = kseq_init(fp1);

		Sketch::MinHash * mh1;
		Sketch::WMinHash * wmh1;
		Sketch::HyperLogLog * hll;
		Sketch::OrderMinHash * omh;

		if(sketchFunc == "MinHash"){
			mh1 = new Sketch::MinHash(KMER_SIZE, MINHASH_SKETCH_SIZE);
		}
		else if(sketchFunc == "WMH"){
			wmh1 = new Sketch::WMinHash(parameter);
		}
		else if(sketchFunc == "HLL"){
			hll = new Sketch::HyperLogLog(HLL_SKETCH_BIT);
		}
		else if(sketchFunc == "OMH"){
			omh = new Sketch::OrderMinHash();
		}
		else{
			fprintf(stderr, "Invaild sketch function: %s\n", sketchFunc.c_str());
			exit(0);
			//return false;
		}


		uint64_t totalLength = 0;
		Vec_SeqInfo curFileSeqs;
		
		while(1){
			int length = kseq_read(ks1);
			if(length < 0){
				break;
			}
			totalLength += length;
			string name = ks1->name.s;
			string comment = ks1->comment.s;
			SequenceInfo tmpSeq{name, comment, 0, length};
			
			if(sketchFunc == "MinHash"){
				mh1->update(ks1->seq.s);
			}
			else if(sketchFunc == "WMH"){
				wmh1->update(ks1->seq.s);
			}
			else if(sketchFunc == "HLL"){
				hll->update(ks1->seq.s);
			}
			else if(sketchFunc == "OMH"){
				omh->buildSketch(ks1->seq.s);
			}

			curFileSeqs.push_back(tmpSeq);
		}//end while, end sketch current file.
		if(sketchFunc == "WMH"){
			wmh1->computeHistoSketch();
		}

		#pragma omp critical
		{
			SketchInfo tmpSketchInfo;
			if(sketchFunc == "MinHash"){
				tmpSketchInfo.minHash = mh1;
			}
			else if(sketchFunc == "WMH"){
				tmpSketchInfo.WMinHash = wmh1;
			}
			else if(sketchFunc == "HLL"){
				tmpSketchInfo.HLL = hll;
			}
			else if(sketchFunc == "OMH"){
				tmpSketchInfo.OMH = omh;
			}

			tmpSketchInfo.id = i;
			tmpSketchInfo.fileName = fileList[i];
			tmpSketchInfo.totalSeqLength = totalLength;
			tmpSketchInfo.fileSeqs = curFileSeqs;
			sketches.push_back(tmpSketchInfo);
		}

		gzclose(fp1);
		kseq_destroy(ks1);
	}//end for
	sort(sketches.begin(), sketches.end(), cmpSketch);

	return true;
}






