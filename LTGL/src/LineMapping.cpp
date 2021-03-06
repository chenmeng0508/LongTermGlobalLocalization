
#include<System.h>

#include"LineMapping.h"
#include"Line3d.h"
#include<LSM/src/LSM.h>
#include<opencv2/core/core.hpp>
#include "LineOptimizer.h"

cv::Mat LineMapping::SkewSymMat(float x, float y, float z) {
	Mat Ax = (Mat_<float>(3, 3, CV_32F) << 0, -z, y, z, 0, -x, -y, x, 0);
	return Ax.clone();
}

cv::Mat LineMapping::SkewSymMat(cv::Mat &m) {
	// when m is a vector composed of 3 values. 
	return SkewSymMat(m.at<float>(0), m.at<float>(1), m.at<float>(2));
}

void LineMapping::Undistort(string &strSettingPath, vector<string> &vstrImageFilenames, string &imgDir) {

	// Load camera parameters from settings file
	cv::FileStorage fSettings(strSettingPath, cv::FileStorage::READ);
	float fx = fSettings["Camera.fx"];
	float fy = fSettings["Camera.fy"];
	float cx = fSettings["Camera.cx"];
	float cy = fSettings["Camera.cy"];

	cv::Mat K = cv::Mat::eye(3, 3, CV_32F);
	K.at<float>(0, 0) = fx;
	K.at<float>(1, 1) = fy;
	K.at<float>(0, 2) = cx;
	K.at<float>(1, 2) = cy;

	cv::Mat DistCoef(4, 1, CV_32F);
	DistCoef.at<float>(0) = fSettings["Camera.k1"];
	DistCoef.at<float>(1) = fSettings["Camera.k2"];
	DistCoef.at<float>(2) = fSettings["Camera.p1"];
	DistCoef.at<float>(3) = fSettings["Camera.p2"];
	const float k3 = fSettings["Camera.k3"];
	if (k3 != 0)
	{
		DistCoef.resize(5);
		DistCoef.at<float>(4) = k3;
	}

	// Simply compute image boundary using first image. 
	Mat im1st = cv::imread(imgDir + "/" + vstrImageFilenames[0], CV_LOAD_IMAGE_UNCHANGED);
	ComputeImageBounds(K, DistCoef, im1st);

	//// copy Keyframe infos. to newly created directory. 
	//string keyframeInfo = imgDir + "/KFinfo.txt";
	//string undistortPath = imgDir + "/undistort";
	//string copiedPath = undistortPath + "/KFinfo.txt";
	//boost::filesystem::copy_file(keyframeInfo, copiedPath, boost::filesystem::copy_option::overwrite_if_exists);

	// If already undistorted, skip. 
	string undistortPath = imgDir + "/undistort";
	boost::filesystem::path dir(undistortPath);
	if (boost::filesystem::create_directory(dir))
	{
		boost::filesystem::path dir_img(undistortPath + "/images");
		boost::filesystem::create_directory(dir_img);
		std::cerr << "Directory Created: " << undistortPath << std::endl;
	}
	else {
		cout << "Already Undistortion done." << endl;
		return;
	}

	int nImages = vstrImageFilenames.size();

	for (int i = 0; i < nImages; i++) {
		// Read image from file
		Mat im = cv::imread(imgDir + "/" + vstrImageFilenames[i], CV_LOAD_IMAGE_UNCHANGED);
		Mat undistort = im.clone();
		cv::undistort(im, undistort, K, DistCoef);

		string outputName = undistortPath + "/" + vstrImageFilenames[i];
		cv::imwrite(outputName, undistort);
	}

	cout << "Undistortion done." << endl;
}

void LineMapping::ComputeImageBounds(const cv::Mat &K, const cv::Mat &distCoef, cv::Mat &im)
{
	if (distCoef.at<float>(0) != 0.0)
	{
		cv::Mat mat(4, 2, CV_32F);
		mat.at<float>(0, 0) = 0.0; mat.at<float>(0, 1) = 0.0;
		mat.at<float>(1, 0) = im.cols; mat.at<float>(1, 1) = 0.0;
		mat.at<float>(2, 0) = 0.0; mat.at<float>(2, 1) = im.rows;
		mat.at<float>(3, 0) = im.cols; mat.at<float>(3, 1) = im.rows;

		// Undistort corners
		mat = mat.reshape(2);
		cv::undistortPoints(mat, mat, K, distCoef, cv::Mat(), K);
		mat = mat.reshape(1);

		float buff = 5.0;

		mnMinX = min(mat.at<float>(0, 0), mat.at<float>(2, 0)) + buff;
		mnMaxX = max(mat.at<float>(1, 0), mat.at<float>(3, 0)) - buff;
		mnMinY = min(mat.at<float>(0, 1), mat.at<float>(1, 1)) + buff;
		mnMaxY = max(mat.at<float>(2, 1), mat.at<float>(3, 1)) - buff;

	}
	else
	{
		mnMinX = 0.0f;
		mnMaxX = im.cols;
		mnMinY = 0.0f;
		mnMaxY = im.rows;
	}
}

void LineMapping::FilterBoundaryLines(cv::Mat &lines) {
	Mat initLines = lines;
	int nRows = initLines.rows;
	int nCols = initLines.cols;
	if (nRows < 1)
		return;

	vector<int> vbadIdx;

	for (int i = 0; i < nRows; i++) {

		float spt_x = lines.at<float>(i, 0);
		float spt_y = lines.at<float>(i, 1);
		float ept_x = lines.at<float>(i, 2);
		float ept_y = lines.at<float>(i, 3);

		if ((spt_x > mnMinX) && (spt_x < mnMaxX)) {
			if ((spt_y > mnMinY) && (spt_y < mnMaxY))
				continue;
		}

		if ((ept_x > mnMinX) && (ept_x < mnMaxX)) {
			if ((ept_y > mnMinY) && (ept_y < mnMaxY))
				continue;
		}

		vbadIdx.push_back(i);
	}

	// Remove rows using bad flags. 
	int nbadLines = vbadIdx.size();
	Mat filteredLines(nRows - nbadLines, nCols, CV_32F);
	int nFilteredCols = nRows - nbadLines;
	if (nbadLines < 1)
		return;

	if (nbadLines == 1) {
		if (vbadIdx[0] > 0)
		{
			cv::Rect rect(0, 0, nCols, vbadIdx[0]);
			initLines(rect).copyTo(filteredLines(rect));
		}
		if (vbadIdx[0] < nRows - 1)
		{
			cv::Rect rect1(0, vbadIdx[0] + 1, nCols, nRows - vbadIdx[0] - 1);
			cv::Rect rect2(0, vbadIdx[0], nCols, nRows - vbadIdx[0] - 1);
			initLines(rect1).copyTo(filteredLines(rect2));
		}
	}

	else {
		for (int j = 0; j < nbadLines; j++) {

			if (j < nbadLines - 1)
				if (vbadIdx[j + 1] - vbadIdx[j] == 1)
					continue;

			if (j == 0) {
				if (vbadIdx[j] == 0) {
					cv::Rect rect1(0, 1, nCols, vbadIdx[1] - 1);
					cv::Rect rect2(0, 0, nCols, vbadIdx[1] - 1);
					initLines(rect1).copyTo(filteredLines(rect2));
				}
				else {
					cv::Rect rect(0, 0, nCols, vbadIdx[0]);
					initLines(rect).copyTo(filteredLines(rect));
				}
			}
			else if (j == nbadLines - 1) {
				if (vbadIdx[j] != nRows - 1) {
					cv::Rect rect1(0, vbadIdx[j] + 1, nCols, nRows - (vbadIdx[j]) - 1);
					cv::Rect rect2(0, vbadIdx[j] - j, nCols, nRows - (vbadIdx[j]) - 1);
					initLines(rect1).copyTo(filteredLines(rect2));
				}
			}
			else {
				cv::Rect rect1(0, vbadIdx[j] + 1, nCols, vbadIdx[j + 1] - (vbadIdx[j]) - 1);
				cv::Rect rect2(0, vbadIdx[j] - j, nCols, vbadIdx[j + 1] - (vbadIdx[j]) - 1);
				initLines(rect1).copyTo(filteredLines(rect2));
			}
		}
	}
	filteredLines.copyTo(lines);
}

float LineMapping::MagMat(cv::Mat &m) {
	float mag = 0;

	for (int i = 0; i < m.rows; i++) {
		for (int j = 0; j < m.cols; j++) {
			mag += m.at<float>(i, j) * m.at<float>(i, j);
		}
	}
	return sqrt(mag);
}

float LineMapping::PointToLineDist(cv::Mat &pt, cv::Mat &line1, cv::Mat &line2) {
	cv::Mat dir_vec = line1.rowRange(0, 2) - line2.rowRange(0, 2);
	cv::Mat normal_vec = (cv::Mat_<float>(2, 1, CV_32F) << -dir_vec.at<float>(1), dir_vec.at<float>(0));
	normal_vec = normal_vec / MagMat(normal_vec);
	cv::Mat vec = pt.rowRange(0, 2) - line2.rowRange(0, 2);
	return abs(normal_vec.dot(vec));
}

void LineMapping::SaveKFinfo(vector<int> vKFindices, string writePath) {
	// Save KF infos into txt. 
	ofstream writeFile(writePath.data());
	if (writeFile.is_open()) {
		for (int i = 0; i < vKFindices.size(); i++) {
			writeFile << to_string(vKFindices[i] + 1) << ".png\n";
		}
		writeFile.close();
	}
}

cv::Mat LineMapping::ComputeFMatrix(const cv::Mat &_T, const cv::Mat &_K) {
	//Compute F Matrix from given Relative Camera Pose.
	//R,t should be relative matrix.(C1 -> C2)
	cv::Mat R = _T.rowRange(0, 3).colRange(0, 3);
	cv::Mat t = _T.rowRange(0, 3).col(3);
	cv::Mat Kinv = _K.inv();
	cv::Mat F = Kinv.t() * SkewSymMat(t) * R * Kinv;
	return F.clone();
}


int LineMapping::TwoViewTriangulation(pair<Mat*, Mat*> _pairLines, pair<Mat*, Mat*> _pairJunctions, const Mat &_K, const Mat &_invK, KeyFrame *_pKF1, KeyFrame *_pKF2, Map *_pMap) {

	int nCreatedLines = 0;
	Mat K = _K;
	Mat invK = _invK;

	// Load matched line information.
	Mat* matchedLines = _pairLines.first;
	Mat* matchedLineIndices = _pairLines.second;
	int nMatchedLines = matchedLines->rows;

	if (nMatchedLines == 0) {
		cout << "Pass triangulation" << endl;
	}

	// Load Junction information.
	Mat* matchedJunctions = _pairJunctions.first;
	Mat* matchedJunctionIndices = _pairJunctions.second;
	int nJunctions = matchedJunctions->rows;

	// variables for saving junction information for Frame1, Frame2.
	map<size_t, set<size_t>> mnslineJunction1, mnslineJunction2;

	cout << "here2" << endl;

	for (int njunctionidx = 0; njunctionidx < nJunctions; njunctionidx++) {
		// For every created juntions, add it to related 2D lines.
		size_t img1LineIdx1 = matchedJunctionIndices->at<int>(njunctionidx, 0);
		size_t img1LineIdx2 = matchedJunctionIndices->at<int>(njunctionidx, 1);
		size_t img2LineIdx1 = matchedJunctionIndices->at<int>(njunctionidx, 2);
		size_t img2LineIdx2 = matchedJunctionIndices->at<int>(njunctionidx, 3);

		mnslineJunction1[img1LineIdx1].insert(img1LineIdx2);
		mnslineJunction1[img1LineIdx2].insert(img1LineIdx1);

		mnslineJunction2[img2LineIdx1].insert(img2LineIdx2);
		mnslineJunction2[img2LineIdx2].insert(img2LineIdx1);

		//JunctionPair *newJP = new JunctionPair(_pKF1, _pKF2, img1LineIdx1, img1LineIdx2, img2LineIdx1, img2LineIdx2);

		//_pKF1->AddJunction2d(img1LineIdx1, img1LineIdx2, newJP);
		//_pKF2->AddJunction2d(img2LineIdx1, img2LineIdx2, newJP);
	}

	cout << "here3" << endl;
	for (map<size_t, set<size_t>>::iterator testit = mnslineJunction1.begin(), testend = mnslineJunction1.end(); testit != testend; testit++) {
		size_t lineidx1 = testit->first;
		set<size_t> setLineidx = testit->second;

		if (setLineidx.size() == 0)
			int bca = 1;
		for (set<size_t>::iterator sit = setLineidx.begin(), send = setLineidx.end(); sit != send; sit++) {
			if (lineidx1 == *sit) {
				int a = 1;
			}
		}
	}


	// First get Plucker Coordinates of triangulated lines.  
	Mat Ocw1 = _pKF1->GetCameraCenter();
	Mat Ocw2 = _pKF2->GetCameraCenter();
	Mat Rcw1 = _pKF1->GetRotation();
	Mat Rcw2 = _pKF2->GetRotation();
	Mat Tcw1 = _pKF1->GetPose();
	Mat Tcw2 = _pKF2->GetPose();
	Mat Twc1 = Tcw1.inv();
	Mat Twc2 = Tcw2.inv();

	for (int i = 0; i < nMatchedLines; i++) {
		Mat matchedPts = matchedLines->row(i);

		size_t nLineIdx1 = matchedLineIndices->at<int>(i, 0);
		size_t nLineIdx2 = matchedLineIndices->at<int>(i, 1);

		// If 3D line has already registered, pass it. 
		Line3d *pLine3d1 = _pKF1->Get3DLine(nLineIdx1);
		Line3d *pLine3d2 = _pKF2->Get3DLine(nLineIdx2);

		// Instead of Fuse process, we simply add observation if the line is already registered. 
		if (pLine3d1) {
			if (pLine3d2) {
				//both are already registered. 
				//Add coplanar information.
				if (pLine3d1 == pLine3d2) {
					pLine3d1->AddCPObservation(_pKF1, mnslineJunction1[nLineIdx1]);
					pLine3d1->AddCPObservation(_pKF2, mnslineJunction2[nLineIdx2]);

				}

				if (pLine3d1->GetNumObservations() < pLine3d1->GetCPLineObservations().size())
					int a = 1;
				continue;
			}
			else {
				// Line1 is already registered so only add observation for Line2. 
				pLine3d1->AddObservation(_pKF2, nLineIdx2);
				_pKF2->AddLine3D(pLine3d1, nLineIdx2);

				//Add coplanar information.
				pLine3d1->AddCPObservation(_pKF1, mnslineJunction1[nLineIdx1]);
				pLine3d1->AddCPObservation(_pKF2, mnslineJunction2[nLineIdx2]);

				if (pLine3d1->GetNumObservations() < pLine3d1->GetCPLineObservations().size())
					int a = 1;

				continue;
			}
		}
		else {
			if (pLine3d2) {
				// Line2 is already registered so only add observation for Line1. 
				pLine3d2->AddObservation(_pKF1, nLineIdx1);
				_pKF1->AddLine3D(pLine3d2, nLineIdx1);

				//Add coplanar information.
				pLine3d2->AddCPObservation(_pKF1, mnslineJunction1[nLineIdx1]);
				pLine3d2->AddCPObservation(_pKF2, mnslineJunction2[nLineIdx2]);

				if (pLine3d2->GetNumObservations() < pLine3d2->GetCPLineObservations().size())
					int a = 1;

				continue;
			}
		}

		Mat Rcw1 = Tcw1.rowRange(0, 3).colRange(0, 3);
		Mat Rcw2 = Tcw2.rowRange(0, 3).colRange(0, 3);

		Mat ptS1 = (Mat_<float>(3, 1, CV_32F) << matchedPts.at<float>(0), matchedPts.at<float>(1), 1);
		Mat ptE1 = (Mat_<float>(3, 1, CV_32F) << matchedPts.at<float>(2), matchedPts.at<float>(3), 1);
		Mat ptS2 = (Mat_<float>(3, 1, CV_32F) << matchedPts.at<float>(4), matchedPts.at<float>(5), 1);
		Mat ptE2 = (Mat_<float>(3, 1, CV_32F) << matchedPts.at<float>(6), matchedPts.at<float>(7), 1);

		// Get normalized coordinates
		Mat normPtS1 = invK * ptS1;
		Mat normPtE1 = invK * ptE1;
		Mat normPtS2 = invK * ptS2;
		Mat normPtE2 = invK * ptE2;

		// Get plane p1 = [px, py, pz, pw] in world coordinates
		Mat plane1 = Mat::zeros(4, 1, CV_32F);
		Mat normalC1 = Mat::zeros(3, 1, CV_32F);   // nomral in C1 coordinate
		normalC1 = SkewSymMat(normPtS1.at<float>(0), normPtS1.at<float>(1), 1) * normPtE1;
		Mat normalW1 = Rcw1.t() * normalC1;
		normalW1.rowRange(0, 3).copyTo(plane1.rowRange(0, 3));
		plane1.at<float>(3) = -normalW1.dot(Ocw1);

		// Get plane p2 = [px', py', pz', pw'] in world coordinates
		Mat plane2 = Mat::zeros(4, 1, CV_32F);
		Mat normalC2 = Mat::zeros(3, 1, CV_32F);   // nomral in C2 coordinate
		normalC2 = SkewSymMat(normPtS2.at<float>(0), normPtS2.at<float>(1), 1) * normPtE2;
		Mat normalW2 = Rcw2.t() * normalC2;
		normalW2.rowRange(0, 3).copyTo(plane2.rowRange(0, 3));
		plane2.at<float>(3) = -normalW2.dot(Ocw2);

		// Triangulate only if we have enough parallax.
		float angle = acos(normalW1.dot(normalW2) / (MagMat(normalW1)*MagMat(normalW2))) * 180 / 3.141592;
		if (abs(angle) < 0.5)
			continue;

		// Get Plucker Coordinate L in world coordinates from Dual Plucker Coordinates 
		Mat dual_L = plane1 * plane2.t() - plane2 * plane1.t();
		Mat d_vector = (Mat_<float>(3, 1, CV_32F) << -dual_L.at<float>(1, 2), dual_L.at<float>(0, 2), -dual_L.at<float>(0, 1));
		Mat n_vector = dual_L.col(3).rowRange(0, 3);
		Mat triangulated_line = Mat::zeros(6, 1, CV_32F);

		// Make direction vector into unit length. 
		//float magNvect = MagMat(d_vector);
		//n_vector = n_vector / magNvect;
		//d_vector = d_vector / magNvect;
		n_vector.copyTo(triangulated_line.rowRange(0, 3));
		d_vector.copyTo(triangulated_line.rowRange(3, 6));

		float depthS1 = ((Ocw2 - Ocw1).dot(normalW2)) / ((Rcw1.t() * normPtS1).dot(normalW2));
		float depthE1 = ((Ocw2 - Ocw1).dot(normalW2)) / ((Rcw1.t() * normPtE1).dot(normalW2));
		float depthS2 = ((Ocw1 - Ocw2).dot(normalW1)) / ((Rcw2.t() * normPtS2).dot(normalW1));
		float depthE2 = ((Ocw1 - Ocw2).dot(normalW1)) / ((Rcw2.t() * normPtE2).dot(normalW1));

		// Depth should be greater than zero. 
		if (depthS1 < 0 || depthE1 < 0 || depthS2 < 0 || depthE2 < 0)
			continue;

		Mat homoPtsS1 = (Mat_<float>(4, 1, CV_32F) << normPtS1.at<float>(0), normPtS1.at<float>(1), normPtS1.at<float>(2), 1 / depthS1);
		Mat point3DS1 = depthS1 * Twc1 * homoPtsS1;
		Mat homoPtsE1 = (Mat_<float>(4, 1, CV_32F) << normPtE1.at<float>(0), normPtE1.at<float>(1), normPtE1.at<float>(2), 1 / depthE1);
		Mat point3DE1 = depthE1 * Twc1 * homoPtsE1;

		Mat homoPtsS2 = (Mat_<float>(4, 1, CV_32F) << normPtS2.at<float>(0), normPtS2.at<float>(1), normPtS2.at<float>(2), 1 / depthS2);
		Mat point3DS2 = depthS2 * Twc2 * homoPtsS2;
		Mat homoPtsE2 = (Mat_<float>(4, 1, CV_32F) << normPtE2.at<float>(0), normPtE2.at<float>(1), normPtE2.at<float>(2), 1 / depthE2);
		Mat point3DE2 = depthE2 * Twc2 * homoPtsE2;

		// Check reprojection error. 
		Mat projectedS2C1 = K * Tcw1.rowRange(0, 3) * point3DS2;
		Mat projectedE2C1 = K * Tcw1.rowRange(0, 3) * point3DE2;
		Mat projectedS1C2 = K * Tcw2.rowRange(0, 3) * point3DS1;
		Mat projectedE1C2 = K * Tcw2.rowRange(0, 3) * point3DE1;

		projectedS2C1 = projectedS2C1 / projectedS2C1.at<float>(2);
		projectedE2C1 = projectedE2C1 / projectedE2C1.at<float>(2);
		projectedS1C2 = projectedS1C2 / projectedS1C2.at<float>(2);
		projectedE1C2 = projectedE1C2 / projectedE1C2.at<float>(2);

		// It should be less than 1. Must be almost identical. 
		float dist1 = PointToLineDist(projectedS2C1, ptS1, ptE1);
		float dist2 = PointToLineDist(projectedE2C1, ptS1, ptE1);
		float dist3 = PointToLineDist(projectedS1C2, ptS2, ptE2);
		float dist4 = PointToLineDist(projectedE1C2, ptS2, ptE2);

		Mat matArray[] = { point3DS1.t() , point3DE1.t(), point3DS2.t(), point3DE2.t() };
		Mat endPts;
		cv::vconcat(matArray, 4, endPts);

		ORB_SLAM2::Line3d *newLine = new ORB_SLAM2::Line3d(triangulated_line, endPts, _pMap);
		newLine->AddObservation(_pKF1, nLineIdx1);
		newLine->AddObservation(_pKF2, nLineIdx2);

		//Add coplanar information.
		newLine->AddCPObservation(_pKF1, mnslineJunction1[nLineIdx1]);
		newLine->AddCPObservation(_pKF2, mnslineJunction2[nLineIdx2]);

		_pKF1->AddLine3D(newLine, nLineIdx1);
		_pKF2->AddLine3D(newLine, nLineIdx2);

		_pMap->AddLine3d(newLine);
		nCreatedLines++;


	}

	return nCreatedLines;
}

int LineMapping::CollectObservations(pair<Mat*, Mat*> _pairLines, pair<Mat*, Mat*> _pairJunctions, const Mat &_K, const Mat &_invK, KeyFrame *_pKF1, KeyFrame *_pKF2, Map *_pMap) {

	int nCreatedLines = 0;
	Mat* matchedLines = _pairLines.first;
	Mat* matchedLineIndices = _pairLines.second;

	int nMatchedLines = matchedLines->rows;

	if (nMatchedLines == 0) {
		cout << "Pass triangulation" << endl;
	}

	for (int i = 0; i < nMatchedLines; i++) {
		Mat matchedPts = matchedLines->row(i);

		// If 3D line has already registered, pass it. 
		Line3d *pLine3d1 = _pKF1->Get3DLine(matchedLineIndices->at<int>(i, 0));
		Line3d *pLine3d2 = _pKF2->Get3DLine(matchedLineIndices->at<int>(i, 1));

		// Instead of Fuse process, we simply add observation if the line is already registered. 
		if (pLine3d1) {
			if (pLine3d2) {
				//both are already registered. 
				continue;
			}
			else {
				// Line1 is already registered so only add observation for Line2. 
				pLine3d1->AddObservation(_pKF2, matchedLineIndices->at<int>(i, 1));
				_pKF2->AddLine3D(pLine3d1, matchedLineIndices->at<int>(i, 1));
				continue;
			}
		}
		else {
			if (pLine3d2) {
				// Line2 is already registered so only add observation for Line1. 
				pLine3d2->AddObservation(_pKF1, matchedLineIndices->at<int>(i, 0));
				_pKF1->AddLine3D(pLine3d2, matchedLineIndices->at<int>(i, 0));
				continue;
			}
		}

		Mat triangulated_line = Mat::zeros(6, 1, CV_32F);
		Mat endPts = Mat::zeros(4, 4, CV_32F);
		ORB_SLAM2::Line3d *newLine = new ORB_SLAM2::Line3d(triangulated_line, endPts, _pMap);
		newLine->AddObservation(_pKF1, matchedLineIndices->at<int>(i, 0));
		newLine->AddObservation(_pKF2, matchedLineIndices->at<int>(i, 1));

		_pKF1->AddLine3D(newLine, matchedLineIndices->at<int>(i, 0));
		_pKF2->AddLine3D(newLine, matchedLineIndices->at<int>(i, 1));

		_pMap->AddLine3d(newLine);
		nCreatedLines++;
	}

	return nCreatedLines;
}

void LineMapping::InitializeLine3dRANSAC(vector<KeyFrame*> _vKFs, Map *_mpMap) {
	vector<KeyFrame*> vpKFS = _vKFs;
	int count = 0;
	/************Search proper inital line parameter via RANSAC *****************************/
	vector<Line3d*> allLine3ds = _mpMap->GetLine3ds();

	for (vector<ORB_SLAM2::Line3d*>::iterator vlineit = allLine3ds.begin(), vlineend = allLine3ds.end(); vlineit != vlineend; vlineit++) {
		// For each of the 3D lines saved in the given frame, we search for proper initialization of line parameter via RANSAC.
		Line3d *pCurrentLine3d = (*vlineit);
		if (!pCurrentLine3d)
			continue;

		map<KeyFrame*, size_t> mAllObervations = pCurrentLine3d->GetObservations();
		int nObs = pCurrentLine3d->GetNumObservations();

		// If it includes only two observation, erase it due to its instability. 
		if (nObs < 3) {
			_mpMap->EraseLine3d(pCurrentLine3d);
			map<KeyFrame*, size_t> tmpObs = pCurrentLine3d->GetObservations();
			for (map<KeyFrame*, size_t>::iterator mObsIt = tmpObs.begin(), mObsEnd = tmpObs.end(); mObsIt != mObsEnd; mObsIt++) {
				KeyFrame* pTmpKF = mObsIt->first;
				size_t sCurrentLine3dIdx = mObsIt->second;

				//Erase 2d line observation.
				pTmpKF->EraseLine3dMatch(pCurrentLine3d);

				// Erase all of coplanar observation for given Line3d on given KF.
				pCurrentLine3d->EraseCPObservations(pTmpKF, sCurrentLine3dIdx);
			}
			//delete pCurrentLine3d;
			continue;
		}

		// Perform RANSAC 
		// First create line model through sampling. 
		// Here, since there would be small number of observations (about maximum 20~30), do exhaustive search. 
		float finalError = 0;
		int finalInliers = 0;
		Mat finalPlucker;
		map<KeyFrame*, bool> finalInlierIdx, tmpInlierIdx;

		vector<KeyFrame*> vObsKFs;
		vObsKFs.reserve(nObs);

		for (map<KeyFrame*, size_t>::iterator mit = mAllObervations.begin(), mend = mAllObervations.end(); mit != mend; mit++) {
			vObsKFs.push_back(mit->first);
			finalInlierIdx[mit->first] = false;
		}

		float th = sqrt(5.994) * 2 * 2;
		//for (int n1 = 0; n1 < min(nObs, 5) - 1; n1++) {
		//	for (int n2 = n1 + 1; n2 < min(nObs, 5); n2++) {
		for (int n1 = 0; n1 < nObs - 1; n1++) {
			for (int n2 = n1 + 1; n2 < nObs; n2++) {
				KeyFrame* pTmpKF1 = *(vObsKFs.begin() + n1);
				KeyFrame* pTmpKF2 = *(vObsKFs.begin() + n2);

				Mat matLine2d1 = pTmpKF1->Get2DLine(mAllObervations[pTmpKF1]);
				Mat matLine2d2 = pTmpKF2->Get2DLine(mAllObervations[pTmpKF2]);

				Mat matTmpPlucker = Mat::zeros(6, 1, CV_32F);

				// Initialize plucker coordinates using given two frames. 
				if (!InitializeLineParam(pTmpKF1, pTmpKF2, matLine2d1, matLine2d2, matTmpPlucker, _mpMap))
					continue;

				// Test Score for created model.
				pair<float, int> eval = ComputeModelScore(matTmpPlucker, pTmpKF1->mK, mAllObervations, tmpInlierIdx, th);

				if (n1 == 0 && n2 == 1) {
					//Initialize the values
					finalError = eval.first;
					finalInliers = eval.second;
					finalPlucker = matTmpPlucker;
					if (tmpInlierIdx.size() > 0)
						finalInlierIdx = tmpInlierIdx;
				}
				else if (eval.second > finalInliers) {
					if (eval.first < finalError) {
						finalError = eval.first;
						finalInliers = eval.second;
						finalPlucker = matTmpPlucker;
						if (tmpInlierIdx.size() > 0)
							finalInlierIdx = tmpInlierIdx;
					}
				}
			}
		}

		//Erase the result when it has less than two inliers.
		if (finalInliers < 2) {
			_mpMap->EraseLine3d(pCurrentLine3d);

			// Erase line observations.
			map<KeyFrame*, size_t> tmpObs = pCurrentLine3d->GetObservations();
			for (map<KeyFrame*, size_t>::iterator mObsIt = tmpObs.begin(), mObsEnd = tmpObs.end(); mObsIt != mObsEnd; mObsIt++) {
				KeyFrame* pTmpKF = mObsIt->first;
				size_t sCurrentLine3dIdx = mObsIt->second;

				//Erase 3d line observation.
				pTmpKF->EraseLine3dMatch(pCurrentLine3d);
				// Erase all of 2d coplanar observations in Line3ds that are coplanar with this line.
				pCurrentLine3d->EraseCPObservations(pTmpKF, sCurrentLine3dIdx);
			}
			//delete pCurrentLine3d;
			continue;
		}

		pCurrentLine3d->SetPluckerWorld(finalPlucker);
		pCurrentLine3d->UpdateEndpts();

		//Erase observation for outliers.
		for (map<KeyFrame*, size_t>::iterator mit = mAllObervations.begin(), mend = mAllObervations.end(); mit != mend; mit++) {
			KeyFrame *pTmpKF = mit->first;
			size_t sCurrentLine3dIdx = mit->second;

			if (!finalInlierIdx[pTmpKF]) {
				//Erase 3d line observation.
				pTmpKF->EraseLine3dMatch(pCurrentLine3d);

				//Erase line observations.
				pCurrentLine3d->EraseObservation(pTmpKF);
					
				// Erase all of 2d coplanar observations in Line3ds that are coplanar with this line.
				pCurrentLine3d->EraseCPObservations(pTmpKF, sCurrentLine3dIdx);
			}
		}
	}	
}

//int LineMapping::InitializeLineParam(pair<Mat*, Mat*> _pairLines, const Mat &_K, const Mat &_invK, KeyFrame *_pKF1, KeyFrame *_pKF2, Map *_pMap)

bool LineMapping::InitializeLineParam(KeyFrame *_pKF1, KeyFrame *_pKF2, const Mat &line2d1, const Mat &line2d2, Mat &tmpPlucker, Map *_pMap) {

	// Initialize line parameter with given two KFs, two 2D lines. 
	Mat K = _pKF1->mK;
	Mat invK = K.inv();

	// First get Plucker Coordinates of triangulated lines.  
	Mat Ocw1 = _pKF1->GetCameraCenter();
	Mat Ocw2 = _pKF2->GetCameraCenter();
	Mat Rcw1 = _pKF1->GetRotation();
	Mat Rcw2 = _pKF2->GetRotation();
	Mat Tcw1 = _pKF1->GetPose();
	Mat Tcw2 = _pKF2->GetPose();
	Mat Twc1 = Tcw1.inv();
	Mat Twc2 = Tcw2.inv();

	Mat ptS1 = (Mat_<float>(3, 1, CV_32F) << line2d1.at<float>(0), line2d1.at<float>(1), 1);
	Mat ptE1 = (Mat_<float>(3, 1, CV_32F) << line2d1.at<float>(2), line2d1.at<float>(3), 1);
	Mat ptS2 = (Mat_<float>(3, 1, CV_32F) << line2d2.at<float>(0), line2d2.at<float>(1), 1);
	Mat ptE2 = (Mat_<float>(3, 1, CV_32F) << line2d2.at<float>(2), line2d2.at<float>(3), 1);

	// Get normalized coordinates
	Mat normPtS1 = invK * ptS1;
	Mat normPtE1 = invK * ptE1;
	Mat normPtS2 = invK * ptS2;
	Mat normPtE2 = invK * ptE2;

	// Get plane p1 = [px, py, pz, pw] in world coordinates
	Mat plane1 = Mat::zeros(4, 1, CV_32F);
	Mat normalC1 = Mat::zeros(3, 1, CV_32F);   // nomral in C1 coordinate
	normalC1 = SkewSymMat(normPtS1.at<float>(0), normPtS1.at<float>(1), 1) * normPtE1;
	Mat normalW1 = Rcw1.t() * normalC1;
	normalW1.rowRange(0, 3).copyTo(plane1.rowRange(0, 3));
	plane1.at<float>(3) = -normalW1.dot(Ocw1);

	// Get plane p2 = [px', py', pz', pw'] in world coordinates
	Mat plane2 = Mat::zeros(4, 1, CV_32F);
	Mat normalC2 = Mat::zeros(3, 1, CV_32F);   // nomral in C2 coordinate
	normalC2 = SkewSymMat(normPtS2.at<float>(0), normPtS2.at<float>(1), 1) * normPtE2;
	Mat normalW2 = Rcw2.t() * normalC2;
	normalW2.rowRange(0, 3).copyTo(plane2.rowRange(0, 3));
	plane2.at<float>(3) = -normalW2.dot(Ocw2);

	float angle = acos(normalW1.dot(normalW2) / (MagMat(normalW1)*MagMat(normalW2))) * 180 / 3.141592;
	if (abs(angle) < 0.5)
		return false;

	// Get Plucker Coordinate L in world coordinates from Dual Plucker Coordinates 
	Mat dual_L = plane1 * plane2.t() - plane2 * plane1.t();
	Mat d_vector = (Mat_<float>(3, 1, CV_32F) << -dual_L.at<float>(1, 2), dual_L.at<float>(0, 2), -dual_L.at<float>(0, 1));
	Mat n_vector = dual_L.col(3).rowRange(0, 3);
	Mat triangulated_line = Mat::zeros(6, 1, CV_32F);

	// Make direction vector into unit length. 
	//float magNvect = MagMat(d_vector);
	//n_vector = n_vector / magNvect;
	//d_vector = d_vector / magNvect;
	n_vector.copyTo(triangulated_line.rowRange(0, 3));
	d_vector.copyTo(triangulated_line.rowRange(3, 6));

	triangulated_line.copyTo(tmpPlucker);

	return true;
}

pair<float, int> LineMapping::ComputeModelScore(const Mat &tmpPlucker, const Mat &K, map<KeyFrame*, size_t> allObservations, map<KeyFrame*, bool> &inlierIndex, const float th) {

	float error = 0;
	int inliers = 0;

	Mat pluckerModel = tmpPlucker;
	Mat n_w = pluckerModel.rowRange(0, 3);
	Mat d_w = pluckerModel.rowRange(3, 6);

	float fx = K.at<float>(0, 0);
	float fy = K.at<float>(1, 1);
	float cx = K.at<float>(0, 2);
	float cy = K.at<float>(1, 2);

	cv::Mat lK = cv::Mat::zeros(3, 3, CV_32FC1);
	lK.at<float>(0, 0) = fy;
	lK.at<float>(1, 1) = fx;
	lK.at<float>(2, 0) = -fy*cx;
	lK.at<float>(2, 1) = -fx*cy;
	lK.at<float>(2, 2) = fx*fy;

	// Compute scores for given model. 
	for (map<KeyFrame*, size_t>::iterator mit = allObservations.begin(), mend = allObservations.end(); mit != mend; mit++) {
		KeyFrame* pTmpKF = mit->first;
		Mat matLine2d = pTmpKF->Get2DLine(mit->second);
		Mat spt = (Mat_<float>(3, 1, CV_32F) << matLine2d.at<float>(0), matLine2d.at<float>(1), 1);
		Mat ept = (Mat_<float>(3, 1, CV_32F) << matLine2d.at<float>(2), matLine2d.at<float>(3), 1);

		Mat Tcw = pTmpKF->GetPose();
		Mat Ocw = pTmpKF->GetCameraCenter();
		Mat Rcw = pTmpKF->GetRotation();
		Mat t = pTmpKF->GetTranslation();
		Mat Twc = Tcw.inv();

		Mat n_c = Rcw * n_w + (SkewSymMat(t)*Rcw) * d_w;
		Mat projectedL2d = lK * n_c;

		float len = sqrt(projectedL2d.at<float>(0)*projectedL2d.at<float>(0) + projectedL2d.at<float>(1)*projectedL2d.at<float>(1));
		float e1 = spt.dot(projectedL2d) / len;
		float e2 = ept.dot(projectedL2d) / len;
		float tmpE = abs(e1) + abs(e2);

		if (abs(tmpE) > th) {
			inlierIndex[pTmpKF] = false;
			continue;
		}

		error += tmpE;
		inliers++;
		inlierIndex[pTmpKF] = true;

	}

	return make_pair(error, inliers);
}


void LineMapping::TestVisualization(vector<ORB_SLAM2::KeyFrame*> vpKFS, string &imgDir, vector<string> &vstrImageFilenames) {
	KeyFrame *pKF1st = *(vpKFS.begin());
	Mat K = pKF1st->mK;
	float fx = K.at<float>(0, 0);
	float fy = K.at<float>(1, 1);
	float cx = K.at<float>(0, 2);
	float cy = K.at<float>(1, 2);

	cv::Mat lK = cv::Mat::zeros(3, 3, CV_32FC1);
	lK.at<float>(0, 0) = fy;
	lK.at<float>(1, 1) = fx;
	lK.at<float>(2, 0) = -fy*cx;
	lK.at<float>(2, 1) = -fx*cy;
	lK.at<float>(2, 2) = fx*fy;

	for (vector<ORB_SLAM2::KeyFrame*>::iterator vit = vpKFS.begin(), vend = vpKFS.end(); vit != vend; vit++) {
		KeyFrame *pKF = (*vit);
		vector<Line3d*> vline3ds = pKF->Get3DLines();


		for (vector<Line3d*>::iterator l3dit = vline3ds.begin(), l3dend = vline3ds.end(); l3dit != l3dend; l3dit++) {
			Line3d *tmpLine3d = (*l3dit);

			if (!tmpLine3d)
				continue;
			int n2dLineIdx = tmpLine3d->GetIndexInKeyFrame(pKF);
			if (n2dLineIdx < 0)
				continue;

			map<KeyFrame*, size_t> tmpObs = tmpLine3d->GetObservations();

			for (map<KeyFrame*, size_t>::iterator mobsit = tmpObs.begin(), mobsend = tmpObs.end(); mobsit != mobsend; mobsit++) {

				KeyFrame* ptmpKF = mobsit->first;
				int ntmpIdx = mobsit->second;
				string imgName = imgDir + "/" + vstrImageFilenames[ptmpKF->mnFrameId];


				Mat Tcw = ptmpKF->GetPose();
				Mat Ocw = ptmpKF->GetCameraCenter();
				Mat Rcw = ptmpKF->GetRotation();
				Mat t = ptmpKF->GetTranslation();
				Mat Twc = Tcw.inv();

				Mat correct2Dline = ptmpKF->Get2DLine(ntmpIdx);

				Mat tmpPlucker = tmpLine3d->GetPluckerWorld();
				Mat n_w = tmpPlucker.rowRange(0, 3);
				Mat d_w = tmpPlucker.rowRange(3, 6);
				Mat n_c = Rcw * n_w + (SkewSymMat(t)*Rcw) * d_w;
				Mat projectedL2d = lK * n_c;

				Mat endpts = tmpLine3d->GetEndPts();
				Mat spt = endpts.row(0);
				Mat ept = endpts.row(1);

				float l1 = projectedL2d.at<float>(0);
				float l2 = projectedL2d.at<float>(1);
				float l3 = projectedL2d.at<float>(2);

				Mat im = imread(imgName);

				Mat left_end = (Mat_<float>(2, 1, CV_32F) << 0, -l3 / l2);
				Mat right_end = (Mat_<float>(2, 1, CV_32F) << im.cols, -(l1 * im.cols + l3) / l2);

				srand(time(NULL));
				int rand1 = rand() % (255 + 1);
				int rand2 = rand() % (255 + 1);
				int rand3 = rand() % (255 + 1);
				line(im, Point(left_end.at<float>(0), left_end.at<float>(1)), Point(right_end.at<float>(0), right_end.at<float>(1)), Scalar(0, 0, 255), 2);
				line(im, Point(correct2Dline.at<float>(0), correct2Dline.at<float>(1)), Point(correct2Dline.at<float>(2), correct2Dline.at<float>(3)), Scalar(0, 255, 0), 2);
				std::stringstream ss;
				ss << "image_" << (ptmpKF->mnFrameId + 1) << ".jpg";
				imshow(ss.str(), im);
			}

			/*line(im, Point(left_end.at<float>(0), left_end.at<float>(1)), Point(right_end.at<float>(0), right_end.at<float>(1)), Scalar(rand1, rand2, rand3), 2);
			line(im, Point(correct2Dline.at<float>(1), correct2Dline.at<float>(0)), Point(correct2Dline.at<float>(3), correct2Dline.at<float>(2)), Scalar(rand1, rand2, rand3), 2);*/
			waitKey(0);
			destroyAllWindows();
			//cout << "left_end" << left_end << endl;
			//cout << "right_end" << right_end << endl;
			//cout << "correct2Dline" << correct2Dline << endl;
			cout << endl;
		}
	}
}

// Set line registration option;
void LineMapping::SetOptions(bool isLineRegisterInitDone, bool isLineRANSACInitDone, bool isLSD) {
	mbIsLineRegisterInit = isLineRegisterInitDone;
	mbIsLineRANSACInit = isLineRANSACInitDone;
	mbIsLSD = isLSD;
}

int LineMapping::LineRegistration(ORB_SLAM2::System &SLAM, vector<string> &vstrImageFilenames, string &writeKFinfo, string &imgDir) {

	ORB_SLAM2::Map* _mpMap = SLAM.GetMap();
	vector<ORB_SLAM2::KeyFrame*> vpKFS = _mpMap->GetAllKeyFrames();

	// Don't change. 
	bool isProvidedLines = true;
	bool isPrecomputedF = true;

	if (mbIsLineRegisterInit) {

		vector<int> vKFindices;
		vKFindices.reserve(vpKFS.size());

		// Prepare for line matching.
		LSM* lineMatching = new LSM(isProvidedLines, isPrecomputedF);

		// Get all of the ids of keyframes. 
		for (vector<ORB_SLAM2::KeyFrame*>::iterator vit = vpKFS.begin(), vend = vpKFS.end(); vit != vend; vit++) {
			vKFindices.push_back((*vit)->mnFrameId);
			sort(vKFindices.begin(), vKFindices.end(), less<int>());
		}

		// Save KF info into text. 
		SaveKFinfo(vKFindices, writeKFinfo);

		int count = 0;
		int totalNlines = 0;
		vector<int> vDoneIdx;
		vDoneIdx.reserve(vpKFS.size());

		// Load lines. 
		cout << "---------Loading lines... ";
		CIO io;
		for (vector<ORB_SLAM2::KeyFrame*>::iterator vit = vpKFS.begin(), vend = vpKFS.end(); vit != vend; vit++) {
			Mat lines;
			ORB_SLAM2::KeyFrame* pCurrentKF = *vit;

			if (!mbIsLSD) {
				// Load provided lines from txt file. 
				//string strLineName = imgDir + "/results/" + to_string(pCurrentKF->mnFrameId + 1) + "_lines.txt";
				string strLineName = imgDir + "/result_wireframe/" + to_string(pCurrentKF->mnFrameId + 1) + "_lines.txt";
				char* lineName = &strLineName[0];
				io.loadData(lineName, lines);
			}
			else {
				// Use LSD. 
				string strImgName = imgDir + "/" + vstrImageFilenames[pCurrentKF->mnFrameId];
				char* imgName = &strImgName[0];
				lineMatching->detectLine(imgName, lines, 30);
			}

			//Remove lines extracted on the boundary(caused by undistortion.)
			FilterBoundaryLines(lines);
			pCurrentKF->SetExtracted2DLines(lines);

		}
		cout << "Done ... ---------" << endl;


		////// Get all of the ids of keyframes. 
		for (vector<ORB_SLAM2::KeyFrame*>::iterator vit = vpKFS.begin(), vend = vpKFS.end(); vit != vend; vit++) {
			// Start with first keyframe. 
			ORB_SLAM2::KeyFrame* pCurrentKF = *vit;
			Mat K = pCurrentKF->mK;
			Mat invK = K.inv();

			cout << count << "/" << vpKFS.size() << "KeyFrames has done. " << endl;
			count++;

			//if (pCurrentKF->mnFrameId != 27) {
			//	continue;
			//}

			// Perform triangulation only for co-visible keyframes. 
			/*******To do : change for colmap.. for now use all the frames ***************/
			//vector<ORB_SLAM2::KeyFrame*> vCovisibleKFs = pCurrentKF->GetBestCovisibilityKeyFrames(15);
			vector<ORB_SLAM2::KeyFrame*> vCovisibleKFs = pCurrentKF->GetBestCovisibilityKeyFrames(vpKFS.size());

			// If covisibility graph is empty, do exhaustive search.
			if (vCovisibleKFs.empty()) {
				vCovisibleKFs.resize(vpKFS.size());
				copy(vpKFS.begin(), vpKFS.end(), vCovisibleKFs.begin());
			}

			string strImgName1 = imgDir + "/" + vstrImageFilenames[pCurrentKF->mnFrameId];
			char* imgName1 = &strImgName1[0];

			// Starts from farthest frame
			for (vector<ORB_SLAM2::KeyFrame*>::reverse_iterator vTmpit = vCovisibleKFs.rbegin(); vTmpit != vCovisibleKFs.rend(); ++vTmpit) {
				ORB_SLAM2::KeyFrame* pTmpKF = *vTmpit;

				if (pCurrentKF->mnFrameId == pTmpKF->mnFrameId) {
					continue;
				}

				//if (pCurrentKF->mnFrameId != 19 || pTmpKF->mnFrameId != 4) {
				//	continue;
				//}

				// If it's already processed, pass. 
				if (find(vDoneIdx.begin(), vDoneIdx.end(), pTmpKF->mnFrameId) != vDoneIdx.end())
					continue;

				// Perform Line Matching First. 
				string strImgName2 = imgDir + "/" + vstrImageFilenames[pTmpKF->mnFrameId];
				char* imgName2 = &strImgName2[0];
				cout << "Processing " << count << " KF / " << vpKFS.size() << " total KFs " << endl;
				cout << imgName1 << endl;
				cout << imgName2 << endl;

				// Set Lines for linematching. 
				lineMatching->setImgLines(imgName1, imgName2, pCurrentKF->GetAll2DLines(), pTmpKF->GetAll2DLines());

				// Compute F Matrix from computed R,t. 
				Mat Tcw1 = pCurrentKF->GetPose();
				Mat Tcw2 = pTmpKF->GetPose();
				Mat T21 = Tcw2 * Tcw1.inv();
				Mat Fmat = ComputeFMatrix(T21, pCurrentKF->mK);

				// matchedLines has a form of  (ps1.x, ps1.y, pe1.x, pe1.y, ps2.x, ps2.y, pe2.x, pe2.y)xN rows.
				if (isPrecomputedF)
					lineMatching->setFmat(Fmat);
				pair<Mat*, Mat*> matchedLineInfo, matchedJunctionInfo;
				lineMatching->lsm(matchedLineInfo, matchedJunctionInfo);
				//cout << "here1" << endl;

				int nCreatedLines = TwoViewTriangulation(matchedLineInfo, matchedJunctionInfo, K, invK, pCurrentKF, pTmpKF, _mpMap);
				//int nCreatedLines = CollectObservations(matchedLines, K, invK, pCurrentKF, pTmpKF, _mpMap);

				totalNlines += nCreatedLines;
				cout << "************* " << nCreatedLines << " lines have newly created.. || Total created lines so far : " << totalNlines << " *************\n" << endl;

				vector<Line3d*> lines = pTmpKF->Get3DLines();
				for (vector<ORB_SLAM2::Line3d*>::iterator vit = lines.begin(), vend = lines.end(); vit != vend; vit++) {
					//Vertex. 
					Line3d* pLine = *vit;
					if (!pLine)
						continue;
					if (pLine->GetNumObservations() < 3)
						continue;
					//ORB_SLAM2::LineOptimizer::LineOptimization(pLine);
					pLine->UpdateEndpts();
				}

			}
			vDoneIdx.push_back(pCurrentKF->mnFrameId);

		}
		cout << "---------Line Registration done. Total " << totalNlines << " lines are created. -------------\n" << endl;
	}

	// Save the map before optimization. 
	int save_mode = 0; // SAVE_MODE : ONLY_MAP(0), LINE_MAP_NOT_OPT(1), LINE_MAP_OPT(2)
	SLAM.SaveMap(save_mode);

	cout << "Wait for key.... " << endl;
	char wait;
	//std::cin >> wait;

	/************Search proper inital line parameter via RANSAC *****************************/
	cout << "----Initializing lines via RANSAC.. ----" << endl;
	if (mbIsLineRANSACInit) {
		InitializeLine3dRANSAC(vpKFS, _mpMap);

		// Save the map before optimization. 
		save_mode = 1; // SAVE_MODE : ONLY_MAP(0), LINE_MAP_NOT_OPT(1), LINE_MAP_OPT(2)
		SLAM.SaveMap(save_mode);
	}
	cout << "----Initialization done. ----" << endl;
	cout << "----Optimizing of all the lines----" << endl;
	cout << "----And Erasing unreliable lines, which include only two observations. -----" << endl;
	int count2 = 0;

	cout << "----Update coplanar line information using junction Information----" << endl;
	vector<Line3d*> unOptLines3D = _mpMap->GetLine3ds();
	for (vector<ORB_SLAM2::Line3d*>::iterator vit = unOptLines3D.begin(), vend = unOptLines3D.end(); vit != vend; vit++) {
		Line3d* pLine = *vit;
		if (!pLine)
			continue;
		pLine->UpdateCoplanarLine3d();
	}

	cout << "----Erase lines with low observations ----" << endl;
	unOptLines3D = _mpMap->GetLine3ds();
	int numberLinesBefore = unOptLines3D.size();
	cout << "----" << numberLinesBefore << " of lines were registered before erase lines with low observations.. " << endl;

	for (vector<ORB_SLAM2::Line3d*>::iterator vit = unOptLines3D.begin(), vend = unOptLines3D.end(); vit != vend; vit++) {
		Line3d* pLine = *vit;
		if (!pLine)
			continue;
		if (pLine->GetNumObservations() < 5) {
			cout << "Erased!" << endl;
			_mpMap->EraseLine3d(pLine);

			// Erase line observations.
			map<KeyFrame*, size_t> tmpObs = pLine->GetObservations();
			for (map<KeyFrame*, size_t>::iterator mObsIt = tmpObs.begin(), mObsEnd = tmpObs.end(); mObsIt != mObsEnd; mObsIt++) {
				KeyFrame* pTmpKF = mObsIt->first;
				size_t sCurrentLine3dIdx = mObsIt->second;

				//Erase 3d line observation.
				pTmpKF->EraseLine3dMatch(pLine);
				// Erase all of 2d coplanar observations in Line3ds that are coplanar with this line.
				pLine->EraseCPObservations(pTmpKF, sCurrentLine3dIdx);
			}

			// Erase Junction observations. 
			pLine->UpdateCoplanarLine3d();
			set<Line3d*> sTmpCPLine3ds = pLine->GetCoplanarLine3d();
			for (set<Line3d*>::iterator sit = sTmpCPLine3ds.begin(), send = sTmpCPLine3ds.end(); sit != send; sit++) {
				Line3d* pTmpLine = *sit;

				//// Erase 2d junction information.
				//map<KeyFrame*, set<size_t>> mTmpCPObs = pTmpLine->GetCPLineObservations();
				//for (map<KeyFrame*, set<size_t>>::iterator mCPLineObit = mTmpCPObs.begin(), mCPLineObend = mTmpCPObs.end(); mCPLineObit != mCPLineObend; mCPLineObit++) {
				//	KeyFrame *pTmpCPKF = mCPLineObit->first;
				//	set<size_t> sAllobs = mCPLineObit->second;
				//	pTmpLine->EraseSingleCPObservation(pTmpCPKF, pLine->GetIndexInKeyFrame(pTmpCPKF));
				//}

				// Erase 3d coplanar line information.
				pTmpLine->EraseCoplanarLine3d(pLine);
			}
			//delete pLine;
			continue;
		}
		pLine->UpdateEndpts();
	}

	// Test whether coplanar lines are correct. 
	bool testCPlines = false;
	if (testCPlines) {
		vector<Line3d*> testLine3d = _mpMap->GetLine3ds();
		for (vector<ORB_SLAM2::Line3d*>::iterator vit = testLine3d.begin(), vend = testLine3d.end(); vit != vend; vit++) {
			Line3d* pLine = *vit;
			if (!pLine)    
				continue;

			map<KeyFrame*, set<size_t>> setLineTest = pLine->GetCPLineObservations();
			for (map<KeyFrame*, set<size_t>>::iterator mit = setLineTest.begin(), mend = setLineTest.end(); mit != mend; mit++) {
				KeyFrame* pKF = mit->first;
				set<size_t> sIdx = mit->second;

				if (sIdx.size() == 0)
					continue;

				string imgName = imgDir + "/" + vstrImageFilenames[pKF->mnFrameId];
				Mat currentLine = pKF->Get2DLine(pLine->GetIndexInKeyFrame(pKF));
				Mat im = imread(imgName);

				set<Line3d*> setLine3ds = pLine->GetCoplanarLine3d();
				pLine->SetIsSelected(true);
				pLine->SetColor(2);

				for (set<Line3d*>::iterator sit = setLine3ds.begin(), send = setLine3ds.end(); sit != send; sit++) {
					(*sit)->SetIsSelected(true);
					(*sit)->SetColor(1);
				}

				line(im, Point(currentLine.at<float>(0), currentLine.at<float>(1)), Point(currentLine.at<float>(2), currentLine.at<float>(3)), Scalar(0, 0, 255), 2);
				for (set<size_t>::iterator ssit = sIdx.begin(), ssend = sIdx.end(); ssit != ssend; ssit++) {
					Mat cpLine = pKF->Get2DLine(*ssit);
					line(im, Point(cpLine.at<float>(0), cpLine.at<float>(1)), Point(cpLine.at<float>(2), cpLine.at<float>(3)), Scalar(0, 255, 0), 2);
				}

				imshow("1", im);
				waitKey(0);
				destroyAllWindows();
			}

			set<Line3d*> setLine3ds = pLine->GetCoplanarLine3d();
			for (set<Line3d*>::iterator sit = setLine3ds.begin(), send = setLine3ds.end(); sit != send; sit++) {
				(*sit)->SetIsSelected(false);
				(*sit)->SetColor(0);
			}
			pLine->SetIsSelected(false);
			pLine->SetColor(0);
		}
	}

	unOptLines3D = _mpMap->GetLine3ds();
	numberLinesBefore = unOptLines3D.size();
	cout << "----" << numberLinesBefore << " of lines were registered before optimization.. " << endl;
	
	for (vector<ORB_SLAM2::Line3d*>::iterator vit = unOptLines3D.begin(), vend = unOptLines3D.end(); vit != vend; vit++) {
		Line3d* pLine = *vit;
		if (!pLine)
			continue;
		if (pLine->GetNumObservations() < 5) {
			cout << "Erased!" << endl;
			_mpMap->EraseLine3d(pLine);

			map<KeyFrame*, size_t> tmpObs = pLine->GetObservations();
			for (map<KeyFrame*, size_t>::iterator mObsIt = tmpObs.begin(), mObsEnd = tmpObs.end(); mObsIt != mObsEnd; mObsIt++) {
				KeyFrame* pTmpKF = mObsIt->first;
				size_t sCurrentLine3dIdx = mObsIt->second;

				//Erase 3d line observation.
				pTmpKF->EraseLine3dMatch(pLine);
				// Erase all of 2d coplanar observations in Line3ds that are coplanar with this line.
				pLine->EraseCPObservations(pTmpKF, sCurrentLine3dIdx);
			}
			//delete pLine;
			continue;
		}
		ORB_SLAM2::LineOptimizer::LineOptimization(pLine);
		pLine->UpdateEndpts();
	}

	cout << "Wait for key.... " << endl;
	std::cin >> wait;

	//ORB_SLAM2::LineOptimizer::LineJunctionOptimization(_mpMap);

	cout << "Wait for key.... " << endl;
	std::cin >> wait;

	cout << "----Optimization done.\n" << endl;
	numberLinesBefore = (_mpMap->GetLine3ds()).size();
	cout << "----" << numberLinesBefore << " of lines were left after optimization.. \n" << endl;

	//// Save the map after optimization. 
	//save_mode = 2; // SAVE_MODE : ONLY_MAP(0), LINE_MAP_NOT_OPT(1), LINE_MAP_OPT(2)
	//SLAM.SaveMap(save_mode);

	cout << "Wait for key.... " << endl;
	std::cin >> wait;

	// Test with visualization by projection on the images. 
	TestVisualization(vpKFS, imgDir, vstrImageFilenames);

	return 0;
}
