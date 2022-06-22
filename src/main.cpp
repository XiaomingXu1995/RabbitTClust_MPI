/*
 * This version is for result checking using single thread.
 * The input parameter and the final output is the same with the final version.
 *
 * The input can be a single file with numbers of sequences to create sketches by sequences.
 * And it can alse be a single file with list of genome files to create sketches by files(genomes).
 * This is strategy of clustering based distance computing by sequences or by genomes.
 *
 * The program includes several sections:
 * section 1: read the input arguments and init the parameters.
 * section 2: read genome files and create the sketches.
 * section 3: compute the distance matrix and generate the Minimum Spanning Tree(MST).
 * section 4: generate the clusters with the MST using different distance threshold.
 *
 * Author: Xiaoming Xu
 * Mar 5, 2021
 *
 */

#include <iostream>
#include "SketchInfo.h"
#include "Sketch.h"// need to add the include path in Makefile.
#include <zlib.h>
#include "MST.h"
#include <omp.h>
#include "UnionFind.h"
#include <algorithm>
#include "parameter.h"
#include "MST_IO.h"
#include <math.h>
#include "Sketch_IO.h"

#ifdef GREEDY_CLUST
#include "greedy.h"
#endif

#include <fstream>
#include <sstream>

using namespace std;

int main(int argc, char * argv[]){


	//section 1: init parameters
	int argIndex = 1;
	string inputFile = "genome.fna";
	string inputFile1 = "genome.info";
	string sketchFunc = "MinHash";
	string outputFile = "result.out";
	int threads = 1;
	bool sketchByFile = false;
	bool isContainment = false;
	bool useFile = false;
	double threshold = 0.05;
	int kmerSize = 21;
	int sketchSize = 1000;
	int containCompress = 10000;
	bool mstLoadSketch = false;
	int denseSpan = 10;
	string mstSketchFile = "sketch.info";
	while(argIndex < argc){
		switch(argv[argIndex][1]){
			case 't':
				threads = atoi(argv[++argIndex]);
				if(threads < 1 || threads > 128){
					fprintf(stderr, "Invalid thread number %d\n", threads);
					return 1;
				}
				break;
			case 'l':
				sketchByFile = true;
				fprintf(stderr, "sketch by file: \n");
				break;
			case 'c':
				isContainment = true;
				containCompress = stoi(argv[++argIndex]);
				threshold = 0.05;
				fprintf(stderr, "compute containment, The sketchSize is in proportion with 1/%d \n", containCompress);
				break;
			case 'k':
				kmerSize = stoi(argv[++argIndex]);
				fprintf(stderr, "set kmerSize: %d\n", kmerSize);
				break;
			case 's':
				sketchSize = stoi(argv[++argIndex]);
				fprintf(stderr, "set sketchSize:  %d\n", sketchSize);
				break;
			case 'd':
				threshold = stod(argv[++argIndex]);
				fprintf(stderr, "set the threshold: %lf \n", threshold);
				break;
			case 'E':
				mstLoadSketch = true;
				fprintf(stderr, "clust-mst load sketches\n");
				break;
			case 'f':
				useFile = true;
				#ifdef GREEDY_CLUST
				fprintf(stderr, "input as sketches informations for greedy incremental cluster\n");
				#else
				fprintf(stderr, "input as MST for Minimum Spanning Tree cluster\n");
				#endif
				break;
			case 'F':
				sketchFunc = argv[++argIndex];
				fprintf(stderr, "the sketch function is: %s \n", sketchFunc.c_str());
				break;
			case 'o':
				outputFile = argv[++argIndex];
				fprintf(stderr, "set output file: %s \n", outputFile.c_str());
				break;
			case 'i':
			{
				if(!useFile && !mstLoadSketch)
				{
					inputFile = argv[++argIndex];
					fprintf(stderr, "the inputFile is: %s \n", inputFile.c_str());
				}
				else if(useFile)
				{
					inputFile = argv[++argIndex];
					if(argIndex == argc)
					{
						fprintf(stderr, "error input File with -f options, exit\n");
						printUsage();
						return 1;
					}
					inputFile1 = argv[++argIndex];
					#ifdef GREEDY_CLUST
					fprintf(stderr, "the genomeInfo and SketchInfo are: %s and %s\n", inputFile.c_str(), inputFile1.c_str());
					#else
					fprintf(stderr, "the genomeInfo and MSTInfo are: %s and %s\n", inputFile.c_str(), inputFile1.c_str());
					#endif
				}
				else if(mstLoadSketch)
				{
					inputFile = argv[++argIndex];
					if(argIndex == argc)
					{
						fprintf(stderr, "error input File with -f options, exit\n");
						printUsage();
						return 1;
					}
					inputFile1 = argv[++argIndex];
					fprintf(stderr, "the genomeInfo and SketchInfo are: %s and %s\n", inputFile.c_str(), inputFile1.c_str());
				}
				break;
			}
			default:
				fprintf(stderr, "Invalid option %s\n", argv[argIndex]);
				printUsage();
				return 1;
		}
		//}
		++argIndex;
	}//end while argument parse;


#ifdef GREEDY_CLUST
	cerr << "use the Greedy cluster" << endl;
#else
	cerr << "use the MST cluster" << endl;
#endif


	vector<SketchInfo> sketches;
	vector<vector<int> > cluster;

	/* For clust-greedy, the input files are GenomeInfo and SketchInfo.
	 * For clust-mst, the input files are GenomeInfo and MSTInfo.
	 * Finished the clustering.
	 */
	if(useFile){
#ifdef GREEDY_CLUST
	#ifdef Timer
	double time0 = get_sec();
	#endif
		sketchByFile = loadSketches(inputFile, inputFile1, threads, sketches);
	#ifdef Timer
	double time1 = get_sec();
	cerr << "========time of load genome Infos and sketch Infos is: " << time1 - time0 << endl;
	#endif
		cluster = greedyCluster(sketches, sketchFunc, threshold, threads);
	#ifdef Timer
	double time2 = get_sec();
	cerr << "========time of greedy incremental cluster is: " << time2 - time1 << endl;
	#endif
#else
	vector<EdgeInfo> mst;
	sketchByFile = loadMSTs(inputFile, inputFile1, sketches, mst);
	int **denseArr;
	loadDense(denseArr, inputFile, denseSpan, sketches);
	vector<EdgeInfo> forest = generateForest(mst, threshold);
	cerr << "finished generate Forest" << endl;
	vector< vector<int> > tmpClust = generateClusterWithBfs(forest, sketches.size());
	printResult(tmpClust, sketches, sketchByFile, outputFile);
	cerr << "write the cluster result into: " << outputFile << endl;
	cerr << "the size of: " << outputFile << " is: " << tmpClust.size() << endl;

	double alpha = 0.05;
	vector<int> totalNoiseArr;
	for(int i = 0; i < tmpClust.size(); i++){
		vector<PairInt> curDenseArr;
		int clustSize = tmpClust[i].size();
		set<int> denseSet;
		for(int j = 0; j < tmpClust[i].size(); j++){
			int element = tmpClust[i][j];
			PairInt p(element, denseArr[0][element]);
			denseSet.insert(denseArr[0][element]);
			curDenseArr.push_back(p);
		}
		vector<int> curNoiseArr = getNoiseNode(curDenseArr, alpha);
		totalNoiseArr.insert(totalNoiseArr.end(), curNoiseArr.begin(), curNoiseArr.end());
	}
	cerr << "the total noiseArr size is: " << totalNoiseArr.size() << endl;
	forest = modifyForest(forest, totalNoiseArr, threads);
	cluster = generateClusterWithBfs(forest, sketches.size());
	string outputFileNew = outputFile + ".removeNoise";
	printResult(cluster, sketches, sketchByFile, outputFileNew);
	cerr << "write the cluster without noise into: " << outputFileNew << endl;
	cerr << "the size of: " << outputFileNew << " is: " << cluster.size() << endl;

	return 0;
//==========================================================
#endif
		printResult(cluster, sketches, sketchByFile, outputFile);
		return 0;//end main 
	}//end useFile

	if(sketchByFile) cerr << "sketch by file!" << endl;
	else cerr << "sketch by sequence!" << endl;

	
	#ifdef DEBUG
	cerr << "the kmerSize is: " << kmerSize << endl;
	cerr << "the thread number is: " << threads << endl;
	cerr << "the threshold is: " << threshold << endl;
	if(isContainment)
		cerr << "the sketchSize is in proportion with 1/" << containCompress << endl;
	else
		cerr << "the sketchSize is: " << sketchSize << endl;
	#endif
	

	#ifdef Timer
	double t0 = get_sec();
	#endif
	//section 2: Sketch Generation, computing from genome file or loading sketches from saved file.

	if(mstLoadSketch){
		sketchByFile = loadSketches(inputFile, inputFile1, threads, sketches);
	}
	else{
		if(!sketchByFile){
			if(!sketchSequences(inputFile, kmerSize, sketchSize, sketchFunc, isContainment, containCompress, sketches, threads)){
				printUsage();
				return 1;
			}
		
		}//end sketch by sequence
		else{
			if(!sketchFiles(inputFile, kmerSize, sketchSize, sketchFunc, isContainment, containCompress, sketches, threads)){
				printUsage();
				return 1;
			}
		}//end sketch by file
	}
	cerr << "the size of sketches(number of genomes or sequences) is: " << sketches.size() << endl;

	#ifdef Timer
	double t1 = get_sec();
	if(mstLoadSketch)
		cerr << "========time of load sketches for clust-mst is: " << t1 - t0 << "========" << endl;
	else
		cerr << "========time of computing sketch is: " << t1 - t0 << "========" << endl;
	#endif

	string folderPath = currentDataTime();
	string command = "mkdir -p " + folderPath;
	system(command.c_str());

	if(!mstLoadSketch){
		saveSketches(sketches, folderPath, inputFile, sketchFunc, isContainment, containCompress, sketchByFile, sketchSize, kmerSize);
	}
	#ifdef Timer
	double t2 = get_sec();
	if(!mstLoadSketch)
		cerr << "========time of saveSketches is: " << t2 - t1 << "========" << endl;
	#endif
	//section 3: generating the clusters.
#ifdef GREEDY_CLUST
	cluster = greedyCluster(sketches, sketchFunc, threshold, threads);
	printResult(cluster, sketches, sketchByFile, outputFile);
	cerr << "write the cluster result into: " << outputFile << endl;
	#ifdef Timer
	double t3 = get_sec();
	cerr << "========time of greedyCluster is: " << t3 - t2 << "========" << endl;
	#endif
#else
	int **denseArr;
	uint64_t* aniArr; //= new uint64_t[101];
	vector<EdgeInfo> mst = modifyMST(sketches, sketchFunc, threads, denseArr, denseSpan, aniArr, outputFile, threshold);
	#ifdef Timer
	double t3 = get_sec();
	cerr << "========time of generateMST is: " << t3 - t2 << "========" << endl;
	#endif
	saveANI(folderPath, inputFile, aniArr, sketchFunc);
	saveDense(folderPath, inputFile, denseArr, denseSpan, sketches);
	saveMST(folderPath, inputFile, sketchFunc, isContainment, containCompress, sketches, mst, sketchByFile, sketchSize, kmerSize);
	#ifdef Timer
	double t4 = get_sec();
	cerr << "========time of saveMST is: " << t4 - t3 << "========" << endl;
	#endif
//=======================================================================================================================
	
	vector<EdgeInfo> forest = generateForest(mst, threshold);
	cerr << "finished generate Forest" << endl;
	vector<vector<int>> tmpClust = generateClusterWithBfs(forest, sketches.size());
	printResult(tmpClust, sketches, sketchByFile, outputFile);
	cerr << "write the cluster result into: " << outputFile << endl;
	cerr << "the size of: " << outputFile << " is: " << tmpClust.size() << endl;
	//update cluster by noise cluster
	double alpha = 0.05;
	vector<int> totalNoiseArr;
	for(int i = 0; i < tmpClust.size(); i++){
		vector<PairInt> curDenseArr;
		int clustSize = tmpClust[i].size();
		set<int> denseSet;
		for(int j = 0; j < tmpClust[i].size(); j++){
			int element = tmpClust[i][j];
			PairInt p(element, denseArr[0][element]);
			denseSet.insert(denseArr[0][element]);
			curDenseArr.push_back(p);
		}
		vector<int> curNoiseArr = getNoiseNode(curDenseArr, alpha);
		totalNoiseArr.insert(totalNoiseArr.end(), curNoiseArr.begin(), curNoiseArr.end());
	}
	cerr << "the total noiseArr size is: " << totalNoiseArr.size() << endl;
	forest = modifyForest(forest, totalNoiseArr, threads);
	cluster = generateClusterWithBfs(forest, sketches.size());
	string outputFileNew = outputFile + ".removeNoise";
	printResult(cluster, sketches, sketchByFile, outputFileNew);
	cerr << "write the cluster without noise into: " << outputFileNew << endl;
	cerr << "the cluster size is: " << cluster.size() << endl;
	cerr << "the size of: " << outputFileNew << " is: " << cluster.size() << endl;
	
	#ifdef Timer
	double t5 = get_sec();
	cerr << "========time of generator forest and cluster is: " << t5 - t4 << "========" << endl;
	#endif
#endif//endif GREEDY_CLUST
	cerr << "finished" << endl;

	return 0;
}//end main





