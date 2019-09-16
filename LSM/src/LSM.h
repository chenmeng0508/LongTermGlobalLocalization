#pragma once
extern "C"
{
#include "lsd.h"
};

#include <stdio.h>
#include <highgui.h>
#include <cv.h>
#include <math.h>
#include <iostream>
#include "IO.h"
#include "LineMatching.h"
#include "PartiallyRecoverConnectivity.h"

// #include "CmdLine.h"
class LSM {
public:
	LSM();
	~LSM();
	LSM(bool isProvideLines, bool isPrecomputedF);
	void setImgLines(char* imgName1, char* imgName2, const Mat &providedLines1, const Mat &providedLines2);
	void detectLine(char* imfile, Mat &mLines, float minLineLength);
	void drawDectectedLine(Mat Img, Mat mLines, string imgName);
	void drawPartiallyConnectedLine(Mat Img, Mat mLines, string imgName, Mat fans);
	pair<Mat*, Mat*> lsm();
	void setFmat(Mat &Fmat) { _Fmat = Fmat; }

public:
	char* mImgName1;
	char* mImgName2;
	Mat mProvidedLines1;
	Mat mProvidedLines2;
	bool mIsProvideLines;
	bool mIsPrecomputedF;

private:
	string outLineMatchesFileName = "result.txt";

	// Precomputed F matrix
	Mat _Fmat;

	// show intermediate results or not
	bool isVerbose = false;

	// whether build image pyramids to deal with scale change betwen images to be matched;
	// Set false when you know there is not scale or only slight scale change between the images,
	// This can tremendously accelerate the matching process.
	bool isBuildingImagePyramids = true;

	// Ture, if load junctions generated by yourself; otherwise false, the program will generate junction by itself
	bool isProvideJunc = false;
	// the paths of your junction files, if load junctions generated by yourself.
	char* providedJunc1;
	char* providedJunc2;

	//// Ture, if load line segments generated by yourself; otherwise false, the program will use LSD to generate segments
	//bool isProvideLines = true;

	// the length requirement for the detected segments used for matching 
	float minLineLength = 0;

	// the threshold controling the size of thte impact region of a segment, see paper for detail
	//float  expandWidth = 20.0;

	// **Changed : For faster computation.
	float  expandWidth = 10.0;

	int   nAvgDesDist = 2;
	bool isScaleChangeExisting = false;
	bool isTwoLineHomography = true;

	// the number of octaves used in the image pyramids
	//int nOctave = 4;

	// **Changed : For faster computation.
	int nOctave = 2;

	// the number of layers in the each octave
	int nOctaveLayer = 2;

	float desDistThrEpi = 0.4;
	float desDistThrProg = 0.5;

	float  fmatThr = 3.0;
	float  hmatThr = 5.0;

	int   nNeighborPts = 10;
	int  nEnterGroup = 4;
	float rotAngThr = 30 * CV_PI / 180;
	float sameSideRatio = 0.8;
	float regionHeight = 4;
	float junctionDistThr = 5.0;

	float intensityProfileWidth = 3;
	float radiusPointMatchUnique = 0;
	float difAngThr = 20 * CV_PI / 180;
	float rcircle = 10;
	float truncateThr = 0.3;
	float fanThr = 1.0 / 4 * CV_PI;

	// Final results of matched lines.
	Mat mlines, mlineIndex;
};
