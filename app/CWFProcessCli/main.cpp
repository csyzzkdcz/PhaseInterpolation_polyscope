#include "polyscope/polyscope.h"
#include "polyscope/pick.h"

#include <igl/invert_diag.h>
#include <igl/per_vertex_normals.h>
#include <igl/readOBJ.h>
#include <igl/writeOBJ.h>
#include <igl/boundary_loop.h>
#include <igl/doublearea.h>
#include <igl/loop.h>
#include <igl/file_dialog_open.h>
#include <igl/file_dialog_save.h>
#include <igl/cotmatrix_entries.h>
#include <igl/cylinder.h>
#include "polyscope/messages.h"
#include "polyscope/point_cloud.h"
#include "polyscope/surface_mesh.h"
#include "polyscope/view.h"

#include <iostream>
#include <filesystem>
#include <utility>

#include "../../include/CommonTools.h"
#include "../../include/MeshLib/MeshUpsampling.h"
#include "../../include/Visualization/PaintGeometry.h"
#include "../../include/Optimization/NewtonDescent.h"
#include "../../include/IntrinsicFormula/InterpolateZvals.h"
#include "../../include/IntrinsicFormula/WrinkleEditingModel.h"

#include "../../include/IntrinsicFormula/WrinkleEditingHalfFullCWF.h"
#include "../../include/IntrinsicFormula/WrinkleEditingFullCWF.h"
#include "../../include/IntrinsicFormula/WrinkleEditingLocalCWF.h"
#include "../../include/IntrinsicFormula/WrinkleEditingCWF.h"
#include "../../include/IntrinsicFormula/WrinkleEditingLinearCWF.h"
#include "../../include/IntrinsicFormula/WrinkleEditingKnoppelCWF.h"

#include "../../include/IntrinsicFormula/KnoppelStripePatternEdgeOmega.h"
#include "../../include/WrinkleFieldsEditor.h"
#include "../../include/SpherigonSmoothing.h"
#include "../../dep/SecStencils/types.h"
#include "../../dep/SecStencils/Subd.h"
#include "../../dep/SecStencils/utils.h"

#include "../../include/json.hpp"
#include "../../include/ComplexLoop/ComplexLoop.h"
#include "../../include/ComplexLoop/ComplexLoopAmpPhase.h"
#include "../../include/ComplexLoop/ComplexLoopAmpPhaseEdgeJump.h"
#include "../../include/ComplexLoop/ComplexLoopReIm.h"
#include "../../include/ComplexLoop/ComplexLoopZuenko.h"
#include "../../include/LoadSaveIO.h"
#include "../../include/SecMeshParsing.h"
#include "../../include/MeshLib/RegionEdition.h"

std::vector<VertexOpInfo> vertOpts;

Eigen::MatrixXd triV, upsampledTriV;
Eigen::MatrixXi triF, upsampledTriF;
MeshConnectivity triMesh;
Mesh secMesh, subSecMesh;

// initial information
Eigen::VectorXd initAmp;
Eigen::VectorXd initOmega;
std::vector<std::complex<double>> initZvals;

// base mesh information list
std::vector<Eigen::VectorXd> omegaList;
std::vector<Eigen::MatrixXd> faceOmegaList;
std::vector<std::vector<std::complex<double>>> zList;


// upsampled informations
std::vector<Eigen::VectorXd> subOmegaList;
std::vector<Eigen::MatrixXd> subFaceOmegaList;

std::vector<Eigen::VectorXd> phaseFieldsList;
std::vector<Eigen::VectorXd> ampFieldsList;
std::vector<std::vector<std::complex<double>>> upZList;
std::vector<Eigen::MatrixXd> wrinkledVList;

std::vector<Eigen::VectorXd> consistencyVec;
std::vector<Eigen::VectorXd> upConsistencyVec;

// reference amp and omega
std::vector<Eigen::VectorXd> refOmegaList;
std::vector<Eigen::VectorXd> refAmpList;

// region edition
RegionEdition regEdt;

int upsampleTimes = 2;

bool isShowVectorFields = true;
bool isShowWrinkels = true;

PaintGeometry mPaint;

int numFrames = 20;
int curFrame = 0;

double globalAmpMax = 1;
double globalAmpMin = 0;

double globalInconMax = 1;
double globalInconMin = 0;

double globalCoarseInconMax = 1;
double globalCoarseInconMin = 0;

double dragSpeed = 0.5;

float vecratio = 0.001;

double gradTol = 1e-6;
double xTol = 0;
double fTol = 0;
int numIter = 1000;
int quadOrder = 4;
float wrinkleAmpScalingRatio = 1;

double spatialAmpRatio = 1;
double spatialEdgeRatio = 1;
double spatialKnoppelRatio = 1;

std::string workingFolder;

std::shared_ptr<IntrinsicFormula::WrinkleEditingModel> editModel;

// smoothing
int smoothingTimes = 3;
double smoothingRatio = 0.95;

bool isFixedBnd = false;
int effectivedistFactor = 4;

bool isSelectAll = false;
std::vector<PickedFace> pickFaces;
VecMotionType selectedMotion = Enlarge;

double selectedMotionValue = 2;
double selectedMagValue = 1;
bool isCoupled = false;

Eigen::VectorXi selectedFids;
Eigen::VectorXi interfaceFids;
Eigen::VectorXi faceFlags;	// -1 for interfaces, 0 otherwise
Eigen::VectorXi selectedVertices;

int optTimes = 5;

bool isLoadOpt;

int clickedFid = -1;
int dilationTimes = 10;

bool isUseV2 = false;
bool isForceReinitilaize = true;


enum ModelType
{
	CWF = 0,
	HalfFullCWF = 1,
	FullCWF = 2,
	LocalCWF = 3,
	LinearCWF = 4,
	KnoppelCWF = 5,
};

InitializationType initType = SeperateLinear;
double zuenkoTau = 0.1;
int zuenkoIter = 5;

ModelType editModelType = CWF;

static void buildEditModel(const ModelType editType, const Eigen::MatrixXd& pos, const MeshConnectivity& mesh, const std::vector<VertexOpInfo>& vertexOpts, const Eigen::VectorXi& faceFlag, int quadOrd, double spatialAmpRatio, double spatialEdgeRatio, double spatialKnoppelRatio, int effectivedistFactor, std::shared_ptr<IntrinsicFormula::WrinkleEditingModel>& editModel)
{
	if (editType == CWF)
	{
		editModel = std::make_shared<IntrinsicFormula::WrinkleEditingCWF>(pos, mesh, vertexOpts, faceFlag, quadOrd, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor);
	}
	else if (editType == HalfFullCWF)
	{
		editModel = std::make_shared<IntrinsicFormula::WrinkleEditingHalfFullCWF>(pos, mesh, vertexOpts, faceFlag, quadOrd, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor);
	}
	else if (editType == FullCWF)
	{
		editModel = std::make_shared<IntrinsicFormula::WrinkleEditingFullCWF>(pos, mesh, vertexOpts, faceFlag, quadOrd, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor);
	}
	else if (editType == LocalCWF)
	{
		editModel = std::make_shared<IntrinsicFormula::WrinkleEditingLocalCWF>(pos, mesh, vertexOpts, faceFlag, quadOrd, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio);
	}
	else if (editType == LinearCWF)
	{
		editModel = std::make_shared<IntrinsicFormula::WrinkleEditingLinearCWF>(pos, mesh, vertexOpts, faceFlag, quadOrd, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor);
	}
	else if (editType == KnoppelCWF)
	{
		editModel = std::make_shared<IntrinsicFormula::WrinkleEditingKnoppelCWF>(pos, mesh, vertexOpts, faceFlag, quadOrd, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor);
	}
	else
	{
		std::cerr << "error model!" << std::endl;
		exit(EXIT_FAILURE);
	}
}

void buildWrinkleMotions(const std::vector<PickedFace>& faceList, std::vector<VertexOpInfo>& vertOpInfo)
{
	int nverts = triV.rows();
	vertOpInfo.clear();
	vertOpInfo.resize(nverts, { None, isCoupled, 0, 1 });

	if (isSelectAll)
	{
		for (int i = 0; i < nverts; i++)
		{
			vertOpts[i] = { selectedMotion, isCoupled, selectedMotionValue, selectedMagValue };
		}
	}
	else
	{
		Eigen::VectorXi tmpFlags;
		tmpFlags.setZero(nverts);
		int nselectedV = 0;

		// make sure there is no overlap
		for (auto& pf : faceList)
		{
			for (auto& v : pf.effectiveVerts)
			{
				if (tmpFlags(v))
				{
					std::cerr << "overlap happens on the effective vertices. " << std::endl;
					exit(EXIT_FAILURE);
				}
				tmpFlags(v) = 1;
				nselectedV++;
			}
		}

		std::cout << "num of selected vertices: " << nselectedV << std::endl;

		vertOpInfo.clear();
		vertOpInfo.resize(nverts, { None, isCoupled, 0, 1 });

		for (auto& pf : faceList)
		{
			for (auto& v : pf.effectiveVerts)
			{
				vertOpInfo[v] = { pf.freqVecMotion, pf.isFreqAmpCoupled, pf.freqVecChangeValue, pf.ampChangeRatio };
			}
		}
	}	

}

bool addSelectedFaces(const PickedFace face, Eigen::VectorXi& curFaceFlags, Eigen::VectorXi& curVertFlags)
{
	for (auto& f : face.effectiveFaces)
		if (curFaceFlags(f))
			return false;

	for (auto& v : face.effectiveVerts)
		if (curVertFlags(v))
			return false;

	for (auto& f : face.effectiveFaces)
		curFaceFlags(f) = 1;
	for (auto& v : face.effectiveVerts)
		curVertFlags(v) = 1;

	return true;
}

void deleteSelectedFaces(const PickedFace face, Eigen::VectorXi& curFaceFlags, Eigen::VectorXi& curVertFlags)
{
	for (auto& f : face.effectiveFaces)
		curFaceFlags(f) = 0;
	for (auto v : face.effectiveVerts)
		curVertFlags(v) = 0;
}

void updateInterfaces(const std::vector<PickedFace>& faces, Eigen::VectorXi& interFaceFlags)
{
	int nfaces = triMesh.nFaces();
	interFaceFlags.setZero(nfaces);

	for (auto& f : faces)
	{
		for (auto& interf : f.interFaces)
			interFaceFlags(interf) = 1;
	}
}

void updateSelectedFaces(const std::vector<PickedFace>& faces, Eigen::VectorXi& selectedFaceFlags)
{
	int nfaces = triMesh.nFaces();
	selectedFaceFlags.setZero(nfaces);

	for (auto& f : faces)
	{
		for (auto& ef : f.effectiveFaces)
			if (selectedFaceFlags(ef))
			{
				std::cerr << "face overlap." << std::endl;
				exit(EXIT_FAILURE);
			}
			else
				selectedFaceFlags(ef) = 1;
	}
}

void updateSelectedRegionSetViz()
{
	updateInterfaces(pickFaces, interfaceFids);
	int nfaces = triF.rows();
	Eigen::MatrixXd renderColor(triF.rows(), 3);
	renderColor.col(0).setConstant(1.0);
	renderColor.col(1).setConstant(1.0);
	renderColor.col(2).setConstant(1.0);

	for (int i = 0; i < nfaces; i++)
	{
		if (selectedFids(i) == 1)
			renderColor.row(i) << 1.0, 0, 0;
		else if (interfaceFids(i) == 1)
			renderColor.row(i) << 0, 1.0, 0;
	}
	polyscope::getSurfaceMesh("base mesh")->addFaceColorQuantity("selected region", renderColor);
}

void addPickedFace(size_t ind)
{
	// Make sure not already used
	for (PickedFace& s : pickFaces)
	{
		if (s.fid == ind)
		{
			std::stringstream ss;
			ss << "Face " << ind;
			std::string vStr = ss.str();
			polyscope::warning("Face " + vStr + " is already picked");
			return;
		}
	}

	PickedFace newface;
	newface.fid = ind;

	newface.buildEffectiveFaces(triF.rows());
	std::cout << "num of new effective faces: " << newface.effectiveFaces.size() << ", dilation radius: " << newface.effectiveRadius << std::endl;


	if (addSelectedFaces(newface, selectedFids, selectedVertices))
		pickFaces.push_back(newface);
	else
	{
		std::stringstream ss;
		ss << "Face " << ind;
		std::string vStr = ss.str();
		polyscope::warning("Face " + vStr + " is inside the effective domain");
		return;
	}
	updateSelectedRegionSetViz();
}

void buildFacesMenu()
{
	if (isSelectAll)
		return;

	bool anyChanged = false;

	ImGui::PushItemWidth(200);

	int id = 0;
	int eraseInd = -1;
	for (PickedFace& s : pickFaces)
	{
		std::stringstream ss;
		ss << "Face " << s.fid;
		std::string vStr = ss.str();
		ImGui::PushID(vStr.c_str());

		ImGui::TextUnformatted(vStr.c_str());

		ImGui::SameLine();
		if (ImGui::Button("delete"))
		{
			eraseInd = id;
			anyChanged = true;
		}
		ImGui::Indent();

		int backupRadius = s.effectiveRadius;

		if (ImGui::InputInt("effective radius", &s.effectiveRadius))
		{
			anyChanged = true;
			deleteSelectedFaces(s, selectedFids, selectedVertices);
			s.buildEffectiveFaces(triF.rows());
			if (!addSelectedFaces(s, selectedFids, selectedVertices))
			{
				std::cout << "due to the overlap, failed to extend the effective radius, back to last effective one: " << backupRadius << std::endl;
				s.effectiveRadius = backupRadius;
				s.buildEffectiveFaces(triF.rows());
				std::cout << "effective face size: " << s.effectiveFaces.size() << std::endl;
				addSelectedFaces(s, selectedFids, selectedVertices);
			}
		}
		if (ImGui::InputInt("interface dilation", &s.interfaceDilation))
		{
			anyChanged = true;
			s.buildEffectiveFaces(triF.rows());
		}
		ImGui::Combo("freq motion", (int*)&s.freqVecMotion, "Ratate\0Tilt\0Enlarge\0None\0");
		if (ImGui::InputDouble("freq change", &s.freqVecChangeValue)) anyChanged = true;
		if (ImGui::InputDouble("amp change", &s.ampChangeRatio)) anyChanged = true;
		if (ImGui::Checkbox("amp freq coupled", &s.isFreqAmpCoupled)) anyChanged = true;
		id++;

		ImGui::Unindent();
		ImGui::PopID();
	}
	ImGui::PopItemWidth();

	// actually do erase, if requested
	if (eraseInd != -1)
	{
		deleteSelectedFaces(pickFaces[eraseInd], selectedFids, selectedVertices);
		pickFaces.erase(pickFaces.begin() + eraseInd);
	}

	if (ImGui::Button("add face"))
	{
		long long int pickId = selectFace(polyscope::getSurfaceMesh("base mesh"));
		//int nverts = polyscope::getSurfaceMesh("base mesh")->nVertices();
		int nfaces = polyscope::getSurfaceMesh("base mesh")->nFaces();

		if (id >= 0 && id < nfaces)
		{
			addPickedFace(pickId);
			anyChanged = true;
		}
	}

	if (anyChanged)
	{
		updateSelectedRegionSetViz();
	}
}


void updateMagnitudePhase(const std::vector<Eigen::VectorXd>& wFrames, const std::vector<std::vector<std::complex<double>>>& zFrames, 
	std::vector<Eigen::VectorXd>& magList, 
	std::vector<Eigen::VectorXd>& phaseList,
	std::vector<std::vector<std::complex<double>>>& upZFrames)
{
	magList.resize(wFrames.size());
	phaseList.resize(wFrames.size());
	upZFrames.resize(wFrames.size());

	consistencyVec.resize(wFrames.size());
	upConsistencyVec.resize(wFrames.size());

	subOmegaList.resize(wFrames.size());
	subFaceOmegaList.resize(wFrames.size());

	MeshConnectivity mesh(triF);

    // ToDo: remove this
    Eigen::SparseMatrix<double> mat;
    std::vector<int> facemap;
    std::vector<std::pair<int, Eigen::Vector3d>> bary;
    meshUpSampling(triV, triF, knoppelUpV, knoppelUpF, upsampleTimes, &mat, &facemap, &bary);
    knoppelPhiList.resize(zFrames.size());


	auto computeMagPhase = [&](const tbb::blocked_range<uint32_t>& range)
	{
		for (uint32_t i = range.begin(); i < range.end(); ++i)
		{
            IntrinsicFormula::getUpsamplingThetaFromEdgeOmega(mesh, wFrames[i], zFrames[i], bary, knoppelPhiList[i]); // knoppel's approach
			
			Eigen::VectorXd edgeVec = swapEdgeVec(triF, wFrames[i], 0);

			consistencyVec[i] = inconsistencyComputation(secMesh, edgeVec, zFrames[i]);

			std::shared_ptr<ComplexLoop> complexLoopOpt = std::make_shared<ComplexLoopZuenko>();
			complexLoopOpt->setBndFixFlag(isFixedBnd);
			complexLoopOpt->SetMesh(secMesh);
			complexLoopOpt->Subdivide(edgeVec, zFrames[i], subOmegaList[i], upZFrames[i], upsampleTimes);
			Mesh tmpMesh = complexLoopOpt->GetMesh();

			upConsistencyVec[i] = inconsistencyComputation(tmpMesh, subOmegaList[i], upZFrames[i]);

			subFaceOmegaList[i] = edgeVec2FaceVec(tmpMesh, subOmegaList[i]);

			magList[i].setZero(upZFrames[i].size());
			phaseList[i].setZero(upZFrames[i].size());

			for (int j = 0; j < magList[i].size(); j++)
			{
				magList[i](j) = std::abs(upZFrames[i][j]);
				phaseList[i](j) = std::arg(upZFrames[i][j]);
			}
		}
	};

	tbb::blocked_range<uint32_t> rangex(0u, (uint32_t)upZFrames.size());
	tbb::parallel_for(rangex, computeMagPhase);

}


void updateWrinkles(const Eigen::MatrixXd& V, const Eigen::MatrixXi& F, const std::vector<std::vector<std::complex<double>>>& zFrames, std::vector<Eigen::MatrixXd>& wrinkledVFrames, double scaleRatio, bool isUseV2)
{
	std::vector<std::vector<std::complex<double>>> interpZList(zFrames.size());
	wrinkledVFrames.resize(zFrames.size());

	std::vector<std::vector<int>> vertNeiEdges;
	std::vector<std::vector<int>> vertNeiFaces;

	buildVertexNeighboringInfo(MeshConnectivity(F), V.rows(), vertNeiEdges, vertNeiFaces);

	auto computeWrinkles = [&](const tbb::blocked_range<uint32_t>& range)
	{
		for (uint32_t i = range.begin(); i < range.end(); ++i)
		{
			getWrinkledMesh(V, F, zFrames[i], &vertNeiFaces, wrinkledVFrames[i], scaleRatio, isUseV2);
		}
	};

	tbb::blocked_range<uint32_t> rangex(0u, (uint32_t)interpZList.size());
	tbb::parallel_for(rangex, computeWrinkles);


}

void getUpsampledMesh(const Eigen::MatrixXd& triV, const Eigen::MatrixXi& triF, Eigen::MatrixXd& upsampledTriV, Eigen::MatrixXi& upsampledTriF)
{
	secMesh = convert2SecMesh(triV, triF);
	subSecMesh = secMesh;

	std::shared_ptr<ComplexLoop> complexLoopOpt = std::make_shared<ComplexLoopZuenko>();
	complexLoopOpt->setBndFixFlag(isFixedBnd);
	complexLoopOpt->SetMesh(secMesh);
	complexLoopOpt->meshSubdivide(upsampleTimes);
	subSecMesh = complexLoopOpt->GetMesh();
	parseSecMesh(subSecMesh, upsampledTriV, upsampledTriF);
}

void initialization(const Eigen::MatrixXd& triV, const Eigen::MatrixXi& triF, Eigen::MatrixXd& upsampledTriV, Eigen::MatrixXi& upsampledTriF)
{
	getUpsampledMesh(triV, triF, upsampledTriV, upsampledTriF);
	selectedFids.setZero(triMesh.nFaces());
	interfaceFids = selectedFids;
	regEdt = RegionEdition(triMesh, triV.rows());
	selectedVertices.setZero(triV.rows());
}


void updateEditionDomain()
{
	int nselected = 0;
	for (int i = 0; i < selectedFids.rows(); i++)
	{
		if (selectedFids(i) == 1)
		{
			nselected++;
		}
	}

	Eigen::VectorXi interfaces;
	updateInterfaces(pickFaces, interfaces);

	faceFlags = selectedFids;
	int ninterfaces = 0;
	for (int i = 0; i < selectedFids.rows(); i++)
	{
		if (selectedFids(i) == 0 && interfaces(i) == 1)
		{
			ninterfaces++;
			faceFlags(i) = -1;
		}
			
	}

	std::cout << "selected effective faces: " << nselected << ", num of interfaces: " << ninterfaces << std::endl;

	std::cout << "build wrinkle motions. " << std::endl;
	buildWrinkleMotions(pickFaces, vertOpts);

}

void updatePaintingItems()
{
	// get interploated amp and phase frames
	std::cout << "compute upsampled phase: " << std::endl;
	updateMagnitudePhase(omegaList, zList, ampFieldsList, phaseFieldsList, upZList);

	std::cout << "compute wrinkle meshes: " << std::endl;
	updateWrinkles(upsampledTriV, upsampledTriF, upZList, wrinkledVList, wrinkleAmpScalingRatio, isUseV2);


	std::cout << "compute face vector fields:" << std::endl;
	faceOmegaList.resize(omegaList.size());
	for (int i = 0; i < omegaList.size(); i++)
	{
		faceOmegaList[i] = intrinsicEdgeVec2FaceVec(omegaList[i], triV, triMesh);
	}


	// update global maximum amplitude
	std::cout << "update max and min amp. " << std::endl;

	globalAmpMax = std::max(ampFieldsList[0].maxCoeff(), refAmpList[0].maxCoeff());
	globalAmpMin = std::min(ampFieldsList[0].minCoeff(), refAmpList[0].minCoeff());
	for (int i = 1; i < ampFieldsList.size(); i++)
	{
		globalAmpMax = std::max(globalAmpMax, std::max(ampFieldsList[i].maxCoeff(), refAmpList[i].maxCoeff()));
		globalAmpMin = std::min(globalAmpMin, std::min(ampFieldsList[i].minCoeff(), refAmpList[i].minCoeff()));
	}

	// update global maximum amplitude
	std::cout << "update max and min consistency. " << std::endl;

	globalInconMax = upConsistencyVec[0].maxCoeff();
	globalInconMin = upConsistencyVec[0].minCoeff();
	for (int i = 1; i < upConsistencyVec.size(); i++)
	{
		globalInconMax = std::max(globalInconMax, upConsistencyVec[i].maxCoeff());
		globalInconMin = std::min(globalInconMin, upConsistencyVec[i].minCoeff());
	}

	// update global maximum amplitude
	std::cout << "update max and min consistency. " << std::endl;

	globalCoarseInconMax = consistencyVec[0].maxCoeff();
	globalCoarseInconMin = consistencyVec[0].minCoeff();
	for (int i = 1; i < upConsistencyVec.size(); i++)
	{
		globalCoarseInconMax = std::max(globalCoarseInconMax, consistencyVec[i].maxCoeff());
		globalCoarseInconMin = std::min(globalCoarseInconMin, consistencyVec[i].minCoeff());
	}

	std::cout << "start to update viewer." << std::endl;
}

void reinitializeKeyFrames(const std::vector<std::complex<double>>& initzvals, const Eigen::VectorXd& initOmega, const Eigen::VectorXi& faceFlags, std::vector<Eigen::VectorXd>& wFrames, std::vector<std::vector<std::complex<double>>>& zFrames, const InitializationType& initType)
{
    std::cout << "model Type: " << editModelType << std::endl;

    /*
    * 		CWF = 0,
			HalfFullCWF = 1,
			FullCWF = 2,
			LocalCWF = 3,
			LinearCWF = 4,
			KnoppelCWF = 5,
    */
    std::cout << "0: CWF (the one used to generate paper results)\n1: Half-Full CWF ((z, w_tar) consistent), no delta omega invovled\n2: Full CWF ((z, w) consistent, no delta omega involved)\n3: Local CWF (Only editing domains are updated, funny behavior for interface domains)\n4: Linear (Linear z and blend w)\n5: Knoppel (blend w and solve Knoppel energy for z)" << std::endl;
    std::cout << "initilization finished with initialization type: (0 for linear, 1 for bnd fixed knoppel)." << initType << std::endl;

	buildEditModel(editModelType, triV, triMesh, vertOpts, faceFlags, quadOrder, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor, editModel);
	editModel->initialization(initzvals, initOmega, numFrames - 2, initType, zuenkoTau, zuenkoIter);

	std::cout << "initilization finished!" << std::endl;
	refOmegaList = editModel->getRefWList();
	refAmpList = editModel->getRefAmpList();

	std::cout << "get w list" << std::endl;
	wFrames = editModel->getWList();
	std::cout << "get z list" << std::endl;
	zFrames = editModel->getVertValsList();
    std::cout << (wFrames[0] - initOmega).norm() << std::endl;

}

void solveKeyFrames(const std::vector<std::complex<double>>& initzvals, const Eigen::VectorXd& initOmega, const Eigen::VectorXi& faceFlags, std::vector<Eigen::VectorXd>& wFrames, std::vector<std::vector<std::complex<double>>>& zFrames)
{
	Eigen::VectorXd x;
	editModel->setSaveFolder(workingFolder);

	std::cout << "model Type: " << editModelType << std::endl;

    /*
    * 		CWF = 0,
			HalfFullCWF = 1,
			FullCWF = 2,
			LocalCWF = 3,
			LinearCWF = 4,
			KnoppelCWF = 5,
    */
    std::cout << "0: CWF (the one used to generate paper results)\n1: Half-Full CWF ((z, w_tar) consistent), no delta omega invovled\n2: Full CWF ((z, w) consistent, no delta omega involved)\n3: Local CWF (Only editing domains are updated, funny behavior for interface domains)\n4: Linear (Linear z and blend w)\n5: Knoppel (blend w and solve Knoppel energy for z)" << std::endl;

	if (isForceReinitilaize)
	{
		buildEditModel(editModelType, triV, triMesh, vertOpts, faceFlags, quadOrder, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor, editModel);
		editModel->initialization(initZvals, initOmega, numFrames - 2, initType, zuenkoTau, zuenkoIter);

		std::cout << "initilization finished with initialization type: (0 for linear, 1 for bnd fixed knoppel)." << initType << std::endl;
	}
	editModel->convertList2Variable(x);
//	editModel->testEnergy(x);

	editModel->solveIntermeditateFrames(x, numIter, gradTol, xTol, fTol, true, workingFolder);
	editModel->convertVariable2List(x);
	refOmegaList = editModel->getRefWList();
	refAmpList = editModel->getRefAmpList();

	std::cout << "get w list" << std::endl;
	wFrames = editModel->getWList();
	std::cout << "get z list" << std::endl;
	zFrames = editModel->getVertValsList();

    std::cout << (wFrames[0] - initOmega).norm() << std::endl;
}

void registerMesh(int frameId, bool isFirstTime = true)
{
	int curShift = 0;
	double shiftx = 1.5 * (triV.col(0).maxCoeff() - triV.col(0).minCoeff());
	if(isFirstTime)
	{
		polyscope::registerSurfaceMesh("base mesh", triV, triF);
	}
	
	polyscope::getSurfaceMesh("base mesh")->addFaceVectorQuantity("frequency field", vecratio * faceOmegaList[frameId], polyscope::VectorType::AMBIENT);
	updateSelectedRegionSetViz();

    Eigen::VectorXd baseAmplitude = refAmpList[frameId];
    for(int i = 0 ; i < refAmpList[frameId].size(); i++)
    {
        baseAmplitude(i) = std::abs(zList[frameId][i]);
    }
    auto baseAmp = polyscope::getSurfaceMesh("base mesh")->addVertexScalarQuantity("opt amplitude", baseAmplitude);
    baseAmp->setMapRange(std::pair<double, double>(globalAmpMin, globalAmpMax));
    polyscope::getSurfaceMesh("base mesh")->addFaceVectorQuantity("opt frequency field", vecratio * faceOmegaList[frameId], polyscope::VectorType::AMBIENT);

	auto consPatterns = polyscope::getSurfaceMesh("base mesh")->addVertexScalarQuantity("inconsistency", consistencyVec[frameId]);
	consPatterns->setMapRange(std::pair<double, double>(globalCoarseInconMin, globalCoarseInconMax));
	curShift++;

	Eigen::MatrixXd refFaceOmega = intrinsicEdgeVec2FaceVec(refOmegaList[frameId], triV, triMesh);
	if(isFirstTime)
	{
		polyscope::registerSurfaceMesh("reference mesh", triV, triF);
		polyscope::getSurfaceMesh("reference mesh")->translate({curShift * shiftx, 0, 0 });
	}
		
	auto refAmp = polyscope::getSurfaceMesh("reference mesh")->addVertexScalarQuantity("reference amplitude", refAmpList[frameId]);
	refAmp->setMapRange(std::pair<double, double>(globalAmpMin, globalAmpMax));
	polyscope::getSurfaceMesh("reference mesh")->addFaceVectorQuantity("reference frequency field", vecratio * refFaceOmega, polyscope::VectorType::AMBIENT);
	curShift++;

	// phase pattern
    if(isShowActualKnoppel && editModelType == KnoppelCWF)
    {
		if(isFirstTime)
		{
			polyscope::registerSurfaceMesh("phase mesh", knoppelUpV, knoppelUpF);
			polyscope::getSurfaceMesh("phase mesh")->translate({ curShift * shiftx, 0, 0 });
		}
        	
        mPaint.setNormalization(false);
        Eigen::MatrixXd phaseColor = mPaint.paintPhi(knoppelPhiList[frameId]);
        polyscope::getSurfaceMesh("phase mesh")->addVertexColorQuantity("vertex phi", phaseColor);
    }
    else
    {
		if(isFirstTime)
		{
			polyscope::registerSurfaceMesh("phase mesh", upsampledTriV, upsampledTriF);
			polyscope::getSurfaceMesh("phase mesh")->translate({ curShift * shiftx, 0, 0 });
		}
        	
        mPaint.setNormalization(false);
        Eigen::MatrixXd phaseColor = mPaint.paintPhi(phaseFieldsList[frameId]);
        polyscope::getSurfaceMesh("phase mesh")->addVertexColorQuantity("vertex phi", phaseColor);
    }
	curShift++;

	// amp pattern
	if(isFirstTime)
	{
		polyscope::registerSurfaceMesh("upsampled ampliude and frequency mesh", upsampledTriV, upsampledTriF);
		polyscope::getSurfaceMesh("upsampled ampliude and frequency mesh")->translate({ curShift * shiftx, 0, 0 });
	}
		
	auto ampPatterns = polyscope::getSurfaceMesh("upsampled ampliude and frequency mesh")->addVertexScalarQuantity("vertex amplitude", ampFieldsList[frameId]);
	ampPatterns->setMapRange(std::pair<double, double>(globalAmpMin, globalAmpMax));
	polyscope::getSurfaceMesh("upsampled ampliude and frequency mesh")->addFaceVectorQuantity("subdivided frequency field", vecratio * subFaceOmegaList[frameId], polyscope::VectorType::AMBIENT);
	auto upconsPatterns = polyscope::getSurfaceMesh("upsampled ampliude and frequency mesh")->addVertexScalarQuantity("inconsistency", upConsistencyVec[frameId]);
	upconsPatterns->setMapRange(std::pair<double, double>(globalInconMin, globalInconMax));
	curShift++;

	// wrinkle mesh
	Eigen::MatrixXd lapWrinkledV = wrinkledVList[frameId];
	laplacianSmoothing(wrinkledVList[frameId], upsampledTriF, lapWrinkledV, smoothingRatio, smoothingTimes, isFixedBnd);
	if(isFirstTime)
	{
		polyscope::registerSurfaceMesh("wrinkled mesh", lapWrinkledV, upsampledTriF);
		polyscope::getSurfaceMesh("wrinkled mesh")->setSurfaceColor({ 80 / 255.0, 122 / 255.0, 91 / 255.0 });
		polyscope::getSurfaceMesh("wrinkled mesh")->translate({ curShift * shiftx, 0, 0 });
	}
		
	else
		polyscope::getSurfaceMesh("wrinkled mesh")->updateVertexPositions(lapWrinkledV);
}

void updateFieldsInView(int frameId, bool isFirstTime)
{
	//std::cout << "update viewer. " << std::endl;
	registerMesh(frameId, isFirstTime);
}


bool loadProblem()
{
	std::string loadFileName = igl::file_dialog_open();

	std::cout << "load file in: " << loadFileName << std::endl;
	using json = nlohmann::json;
	std::ifstream inputJson(loadFileName);
	if (!inputJson) {
		std::cerr << "missing json file in " << loadFileName << std::endl;
		return false;
	}

	std::string filePath = loadFileName;
	std::replace(filePath.begin(), filePath.end(), '\\', '/'); // handle the backslash issue for windows
	int id = filePath.rfind("/");
	workingFolder = filePath.substr(0, id + 1);
	std::cout << "working folder: " << workingFolder << std::endl;

	json jval;
	inputJson >> jval;

	std::string meshFile = jval["mesh_name"];
	upsampleTimes = jval["upsampled_times"];
	if (upsampleTimes > 2)
		upsampleTimes = 2;


	meshFile = workingFolder + meshFile;
	igl::readOBJ(meshFile, triV, triF);
	triMesh = MeshConnectivity(triF);
	initialization(triV, triF, upsampledTriV, upsampledTriF);
	

	quadOrder = jval["quad_order"];
	numFrames = jval["num_frame"];

	isSelectAll = jval["region_global_details"]["select_all"];
	isCoupled = jval["region_global_details"]["amp_omega_coupling"];
	selectedMagValue = jval["region_global_details"]["amp_operation_value"];
	selectedMotionValue = jval["region_global_details"]["omega_operation_value"];
	std::string optype = jval["region_global_details"]["omega_operation_motion"];

	/*isCoupled = jval["operation_details"]["amp_omega_coupling"];
	selectedMagValue = jval["operation_details"]["amp_operation_value"];
	selectedMotionValue = jval["operation_details"]["omega_operation_value"];

	std::string optype = jval["operation_details"]["omega_operation_type"];*/
	if (optype == "None")
		selectedMotion = None;
	else if (optype == "Enlarge")
		selectedMotion = Enlarge;
	else if (optype == "Rotate")
		selectedMotion = Rotate;
	else
		selectedMotion = None;

	/*isSelectAll = jval["region_details"]["select_all"];
	clickedFid = jval["region_details"]["selected_fid"];
	dilationTimes = jval["region_details"]["selected_domain_dilation"];
	optTimes = jval["region_details"]["interface_dilation"];*/

	pickFaces.clear();

	if (jval.contains(std::string_view{ "region_local_details" }))
	{
		int npicked = jval["region_local_details"].size();
		for (int i = 0; i < npicked; i++)
		{
			PickedFace pf;
			pf.fid = jval["region_local_details"][i]["face_id"];
			pf.effectiveRadius = jval["region_local_details"][i]["effective_radius"];
			pf.interfaceDilation = jval["region_local_details"][i]["interface_dilation"];

			optype = jval["region_local_details"][i]["omega_operation_motion"];
			if (optype == "None")
				pf.freqVecMotion = None;
			else if (optype == "Enlarge")
				pf.freqVecMotion = Enlarge;
			else if (optype == "Rotate")
				pf.freqVecMotion = Rotate;
			else
				pf.freqVecMotion = None;

			pf.isFreqAmpCoupled = jval["region_local_details"][i]["amp_omega_coupling"];
			pf.freqVecChangeValue = jval["region_local_details"][i]["omega_opereation_value"];
			pf.ampChangeRatio = jval["region_local_details"][i]["amp_operation_value"];
			

			pf.buildEffectiveFaces(triF.rows());
			if (!addSelectedFaces(pf, selectedFids, selectedVertices))
			{
				std::cerr << "something wrong happened in the store setup file!" << std::endl;
				exit(EXIT_FAILURE);
			}
			pickFaces.push_back(pf);
		}
	}
	std::cout << "num of picked faces: " << pickFaces.size() << std::endl;
	
	updateEditionDomain();
	

	if (jval.contains(std::string_view{ "spatial_ratio" }))
	{
		if (jval["spatial_ratio"].contains(std::string_view{ "amp_ratio" }))
			spatialAmpRatio = jval["spatial_ratio"]["amp_ratio"];
		else
			spatialAmpRatio = 100;

		if (jval["spatial_ratio"].contains(std::string_view{ "edge_ratio" }))
			spatialEdgeRatio = jval["spatial_ratio"]["edge_ratio"];
		else
			spatialEdgeRatio = 100;

		if (jval["spatial_ratio"].contains(std::string_view{ "knoppel_ratio" }))
			spatialKnoppelRatio = jval["spatial_ratio"]["knoppel_ratio"];
		else
			spatialKnoppelRatio = 100;
	}
	else
	{
		spatialAmpRatio = 100;
		spatialEdgeRatio = 100;
		spatialKnoppelRatio = 100;
	}


	int nedges = triMesh.nEdges();
	int nverts = triV.rows();

	std::string initAmpPath = jval["init_amp"];
	std::string initOmegaPath = jval["init_omega"];
	std::string initZValsPath = "zvals.txt";
	if (jval.contains(std::string_view{ "init_zvals" }))
	{
		initZValsPath = jval["init_zvals"];
	}

	if (!loadEdgeOmega(workingFolder + initOmegaPath, nedges, initOmega)) {
		std::cout << "missing init edge omega file." << std::endl;
		return false;
	}

	if (!loadVertexZvals(workingFolder + initZValsPath, triV.rows(), initZvals))
	{
		std::cout << "missing init zval file, try to load amp file, and round zvals from amp and omega" << std::endl;
		if (!loadVertexAmp(workingFolder + initAmpPath, triV.rows(), initAmp))
		{
			std::cout << "missing init amp file: " << std::endl;
			return false;
		}

		else
		{
			Eigen::VectorXd edgeArea, vertArea;
			edgeArea = getEdgeArea(triV, triMesh);
			vertArea = getVertArea(triV, triMesh);
			IntrinsicFormula::roundZvalsFromEdgeOmegaVertexMag(triMesh, initOmega, initAmp, edgeArea, vertArea, triV.rows(), initZvals);
		}
	}
	else
	{
		initAmp.setZero(triV.rows());
		for (int i = 0; i < initZvals.size(); i++)
			initAmp(i) = std::abs(initZvals[i]);
	}


	std::string refAmp = jval["reference"]["ref_amp"];
	std::string refOmega = jval["reference"]["ref_omega"];

	std::string optZvals = jval["solution"]["opt_zvals"];
	std::string optOmega = jval["solution"]["opt_omega"];

	// edge omega List
	int iter = 0;
	bool isLoadRef = true;
	refAmpList.resize(numFrames);
	refOmegaList.resize(numFrames);

	for (uint32_t i = 0; i < numFrames; ++i) {

		if (!loadVertexAmp(workingFolder + refAmp + "/amp_" + std::to_string(i) + ".txt", triV.rows(), refAmpList[i]))
		{
			std::cout << "missing amp file: " << std::endl;
			isLoadRef = false;
			break;
		}

		std::string edgePath = workingFolder + refOmega + "/omega_" + std::to_string(i) + ".txt";
		if (!loadEdgeOmega(edgePath, nedges, refOmegaList[i])) {
			std::cout << "missing edge file." << std::endl;
			isLoadRef = false;
			break;
		}
	}


	isLoadOpt = true;
	zList.clear();
	omegaList.clear();
	for (int i = 0; i < numFrames; i++)
	{
		std::string zvalFile = workingFolder + optZvals + "/zvals_" + std::to_string(i) + ".txt";
		std::string edgeOmegaFile = workingFolder + optOmega + "/omega_" + std::to_string(i) + ".txt";
		std::vector<std::complex<double>> zvals;
		if (!loadVertexZvals(zvalFile, nverts, zvals))
		{
			isLoadOpt = false;
			break;
		}
		Eigen::VectorXd edgeOmega;
		if (!loadEdgeOmega(edgeOmegaFile, nedges, edgeOmega)) {
			isLoadOpt = false;
			break;
		}

		zList.push_back(zvals);
		omegaList.push_back(edgeOmega);
	}

	if (isLoadOpt)
	{
		std::cout << "load zvals and omegas from file!" << std::endl;
	}
	if (!isLoadOpt || !isLoadRef)
	{
		buildEditModel(editModelType, triV, triMesh, vertOpts, faceFlags, quadOrder, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor, editModel);

		editModel->initialization(initZvals, initOmega, numFrames - 2, initType, 0.1);
		refAmpList = editModel->getRefAmpList();
		refOmegaList = editModel->getRefWList();

		if (!isLoadOpt)
		{
			zList = editModel->getVertValsList();
			omegaList = editModel->getWList();
		}
		
	}
	buildEditModel(editModelType, triV, triMesh, vertOpts, faceFlags, quadOrder, spatialAmpRatio, spatialEdgeRatio, spatialKnoppelRatio, effectivedistFactor, editModel);
	editModel->initialization(zList, omegaList, refAmpList, refOmegaList);

	updatePaintingItems();

	curFrame = 0;

	return true;
}


bool saveProblem()
{
	std::string saveFileName = igl::file_dialog_save();

	std::string curOpt = "None";
	if (selectedMotion == Enlarge)
		curOpt = "Enlarge";
	else if (selectedMotion == Rotate)
		curOpt = "Rotate";

	using json = nlohmann::json;
	json jval =
	{
			{"mesh_name",         "mesh.obj"},
			{"num_frame",         zList.size()},
			{"quad_order",        quadOrder},
			{"spatial_ratio",     {
										   {"amp_ratio", spatialAmpRatio},
										   {"edge_ratio", spatialEdgeRatio},
										   {"knoppel_ratio", spatialKnoppelRatio}

								  }
			},
			{"upsampled_times",  upsampleTimes},
			{"init_omega",        "omega.txt"},
			{"init_amp",          "amp.txt"},
			{"init_zvls",         "zvals.txt"},
			{"region_global_details",	  {
										{"select_all", isSelectAll},
										{"omega_operation_motion", curOpt},
										{"omega_operation_value", selectedMotionValue},
										{"amp_omega_coupling", isCoupled},
										{"amp_operation_value", selectedMagValue}
								  }
			},
			{
			 "reference",         {
										  {"ref_amp", "/refAmp/"},
										  {"ref_omega", "/refOmega/"}
								  }
			},
			{
			 "solution",          {
										  {"opt_zvals", "/optZvals/"},
										  {"opt_omega", "/optOmega/"},
										  {"wrinkle_mesh", "/wrinkledMesh/"},
										  {"upsampled_amp", "/upsampledAmp/"},
										  {"upsampled_phase", "/upsampledPhase/"}
								  }
			}
	};

	for (int i = 0; i < pickFaces.size(); i++)
	{
		curOpt = "None";
		if (pickFaces[i].freqVecMotion == Enlarge)
			curOpt = "Enlarge";
		else if (pickFaces[i].freqVecMotion == Rotate)
			curOpt = "Rotate";
		json pfJval =
		{
			{"face_id", pickFaces[i].fid},
			{"effective_radius", pickFaces[i].effectiveRadius},
			{"interface_dilation", pickFaces[i].interfaceDilation},
			{"omega_operation_motion", curOpt},
			{"omega_opereation_value", pickFaces[i].freqVecChangeValue},
			{"amp_operation_value", pickFaces[i].ampChangeRatio},
			{"amp_omega_coupling", pickFaces[i].isFreqAmpCoupled}
		};
		jval["region_local_details"].push_back(pfJval);
	}

	std::string filePath = saveFileName;
	std::replace(filePath.begin(), filePath.end(), '\\', '/'); // handle the backslash issue for windows
	int id = filePath.rfind("/");
	std::string workingFolder = filePath.substr(0, id + 1);


    saveEdgeOmega(workingFolder + "omega.txt", initOmega);
    saveVertexAmp(workingFolder + "amp.txt", initAmp);
    saveVertexZvals(workingFolder + "zvals.txt", initZvals);

	igl::writeOBJ(workingFolder + "mesh.obj", triV, triF);

	std::string outputFolder = workingFolder + "/optZvals/";
	mkdir(outputFolder);

	std::string omegaOutputFolder = workingFolder + "/optOmega/";
	mkdir(omegaOutputFolder);

    std::string refOmegaOutputFolder = workingFolder + "/refOmega/";
    mkdir(refOmegaOutputFolder);

    // save reference
    std::string refAmpOutputFolder = workingFolder + "/refAmp/";
    mkdir(refAmpOutputFolder);

    int nframes = zList.size();
    auto savePerFrame = [&](const tbb::blocked_range<uint32_t>& range)
    {
        for (uint32_t i = range.begin(); i < range.end(); ++i)
        {

            saveVertexZvals(outputFolder + "zvals_" + std::to_string(i) + ".txt", zList[i]);
            saveEdgeOmega(omegaOutputFolder + "omega_" + std::to_string(i) + ".txt", omegaList[i]);
            saveVertexAmp(refAmpOutputFolder + "amp_" + std::to_string(i) + ".txt", refAmpList[i]);
            saveEdgeOmega(refOmegaOutputFolder + "omega_" + std::to_string(i) + ".txt", refOmegaList[i]);
        }
    };

    tbb::blocked_range<uint32_t> rangex(0u, (uint32_t)nframes, GRAIN_SIZE);
    tbb::parallel_for(rangex, savePerFrame);

//	for (int i = 0; i < zList.size(); i++)
//	{
//        saveVertexZvals(outputFolder + "zvals_" + std::to_string(i) + ".txt", zList[i]);
//        saveEdgeOmega(omegaOutputFolder + "omega_" + std::to_string(i) + ".txt", omegaList[i]);
//        saveVertexAmp(refAmpOutputFolder + "amp_" + std::to_string(i) + ".txt", refAmpList[i]);
//        saveEdgeOmega(refOmegaOutputFolder + "omega_" + std::to_string(i) + ".txt", refOmegaList[i]);
//	}


	std::ofstream o(saveFileName);
	o << std::setw(4) << jval << std::endl;
	std::cout << "save file in: " << saveFileName << std::endl;

	return true;
}

bool saveForRender()
{
    std::string saveFileName = igl::file_dialog_save();
    std::string filePath = saveFileName;
    std::replace(filePath.begin(), filePath.end(), '\\', '/'); // handle the backslash issue for windows
    int id = filePath.rfind("/");
    std::string workingFolder = filePath.substr(0, id + 1);

    // render information
    std::string renderFolder = workingFolder + "/render/";
    mkdir(renderFolder);
    igl::writeOBJ(renderFolder + "basemesh.obj", triV, triF);
    igl::writeOBJ(renderFolder + "upmesh.obj", upsampledTriV, upsampledTriF);

    if(editModelType == KnoppelCWF)
        igl::writeOBJ(renderFolder + "knoppelUpmesh.obj", knoppelUpV, knoppelUpF);

    saveFlag4Render(faceFlags, renderFolder + "faceFlags.cvs");

    std::string outputFolderAmp = renderFolder + "/upsampledAmp/";
    mkdir(outputFolderAmp);

    std::string outputFolderPhase = renderFolder + "/upsampledPhase/";
    mkdir(outputFolderPhase);

    std::string outputFolderKnoppelPhase = renderFolder + "/upsampledKnoppelPhase/";
    if(editModelType == KnoppelCWF)
        mkdir(outputFolderKnoppelPhase);


    std::string outputFolderWrinkles = renderFolder + "/wrinkledMesh/";
    mkdir(outputFolderWrinkles);

    std::string refAmpFolder = renderFolder + "/refAmp/";
    mkdir(refAmpFolder);
    std::string refOmegaFolder = renderFolder + "/refOmega/";
    mkdir(refOmegaFolder);

    std::string optAmpFolder = renderFolder + "/optAmp/";
    mkdir(optAmpFolder);
    std::string optOmegaFolder = renderFolder + "/optOmega/";
    mkdir(optOmegaFolder);

    int nframes = ampFieldsList.size();

    auto savePerFrame = [&](const tbb::blocked_range<uint32_t>& range)
    {
        for (uint32_t i = range.begin(); i < range.end(); ++i)
        {

            // upsampled information
            igl::writeOBJ(outputFolderWrinkles + "wrinkledMesh_" + std::to_string(i) + ".obj", wrinkledVList[i], upsampledTriF);
            Eigen::MatrixXd lapWrinkledV;
            laplacianSmoothing(wrinkledVList[i], upsampledTriF, lapWrinkledV, smoothingRatio, smoothingTimes, isFixedBnd);
            igl::writeOBJ(outputFolderWrinkles + "wrinkledMeshSmoothed_" + std::to_string(i) + ".obj", lapWrinkledV, upsampledTriF);

            saveAmp4Render(ampFieldsList[i], outputFolderAmp + "upAmp_" + std::to_string(i) + ".cvs", globalAmpMin, globalAmpMax);
            savePhi4Render(phaseFieldsList[i], outputFolderPhase + "upPhase" + std::to_string(i) + ".cvs");
            if(editModelType == KnoppelCWF)
                savePhi4Render(knoppelPhiList[i], outputFolderKnoppelPhase + "upKnoppelPhase" + std::to_string(i) + ".cvs");
            saveDphi4Render(subFaceOmegaList[i], subSecMesh, outputFolderPhase + "upOmega" + std::to_string(i) + ".cvs");

            // reference information
            Eigen::MatrixXd refFaceOmega = intrinsicEdgeVec2FaceVec(refOmegaList[i], triV, triMesh);
            saveAmp4Render(refAmpList[i], refAmpFolder + "refAmp_" + std::to_string(i) + ".cvs", globalAmpMin, globalAmpMax);
            saveDphi4Render(refFaceOmega, triMesh, triV, refOmegaFolder + "refOmega_" + std::to_string(i) + ".cvs");

            // optimal information
            saveDphi4Render(faceOmegaList[i], triMesh, triV, optOmegaFolder + "optOmega_" + std::to_string(i) + ".cvs");
            Eigen::VectorXd baseAmplitude = refAmpList[i];
            for(int j = 0 ; j < refAmpList[i].size(); j++)
            {
                baseAmplitude(j) = std::abs(zList[i][j]);
            }

            saveAmp4Render(baseAmplitude, optAmpFolder + "optAmp_" + std::to_string(i) + ".cvs", globalAmpMin, globalAmpMax);
        }
    };

    tbb::blocked_range<uint32_t> rangex(0u, (uint32_t)nframes);
    tbb::parallel_for(rangex, savePerFrame);

//    for (int i = 0; i < nframes; i++)
//    {
//        // upsampled information
//        igl::writeOBJ(outputFolderWrinkles + "wrinkledMesh_" + std::to_string(i) + ".obj", wrinkledVList[i], upsampledTriF);
//        Eigen::MatrixXd lapWrinkledV;
//        laplacianSmoothing(wrinkledVList[i], upsampledTriF, lapWrinkledV, smoothingRatio, smoothingTimes, isFixedBnd);
//        igl::writeOBJ(outputFolderWrinkles + "wrinkledMeshSmoothed_" + std::to_string(i) + ".obj", lapWrinkledV, upsampledTriF);
//
//        saveAmp4Render(ampFieldsList[i], outputFolderAmp + "upAmp_" + std::to_string(i) + ".cvs");
//        savePhi4Render(phaseFieldsList[i], outputFolderPhase + "upPhase" + std::to_string(i) + ".cvs");
//        saveDphi4Render(subFaceOmegaList[i], subSecMesh, outputFolderPhase + "upOmega" + std::to_string(i) + ".cvs");
//
//        // reference information
//        Eigen::MatrixXd refFaceOmega = intrinsicEdgeVec2FaceVec(refOmegaList[i], triV, triMesh);
//        saveAmp4Render(refAmpList[i], refAmpFolder + "refAmp_" + std::to_string(i) + ".cvs");
//        saveDphi4Render(refFaceOmega, triMesh, triV, refOmegaFolder + "refOmega_" + std::to_string(i) + ".cvs");
//
//        // optimal information
//        saveDphi4Render(faceOmegaList[i], triMesh, triV, optOmegaFolder + "optOmega_" + std::to_string(i) + ".cvs");
//        saveAmp4Render(zList[i], optAmpFolder + "optAmp_" + std::to_string(i) + ".cvs");
//    }

    return true;
}

void callback() {
	ImGui::PushItemWidth(100);
	float w = ImGui::GetContentRegionAvailWidth();
	float p = ImGui::GetStyle().FramePadding.x;
	if (ImGui::Button("Load", ImVec2((w - p) / 2.f, 0)))
	{
		loadProblem();
		updateMagnitudePhase(omegaList, zList, ampFieldsList, phaseFieldsList, upZList);
		updateFieldsInView(curFrame, true);
	}
	ImGui::SameLine(0, p);
	if (ImGui::Button("Save", ImVec2((w - p) / 2.f, 0)))
	{
		saveProblem();
	}
	if (ImGui::Button("save for render", ImVec2(-1, 0)))
	{
		saveForRender();
	}
	ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
	if (ImGui::BeginTabBar("Visualization Options", tab_bar_flags))
	{
		if (ImGui::BeginTabItem("Wrinkle Mesh Upsampling"))
		{
			if (ImGui::InputInt("upsampled level", &upsampleTimes))
            {
                if (upsampleTimes >= 0)
                {
                    getUpsampledMesh(triV, triF, upsampledTriV, upsampledTriF);
                    updatePaintingItems();
                    updateFieldsInView(curFrame, true);
                }
            }
			if (ImGui::Checkbox("fix bnd", &isFixedBnd))
			{
				getUpsampledMesh(triV, triF, upsampledTriV, upsampledTriF);
				updatePaintingItems();
				updateFieldsInView(curFrame, true);
			}
            if (ImGui::Checkbox("show actual knoppel", &isShowActualKnoppel))
            {
                getUpsampledMesh(triV, triF, upsampledTriV, upsampledTriF);
                updatePaintingItems();
                updateFieldsInView(curFrame, true);
            }
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Wrinkle Mesh Smoothing"))
		{
			if (ImGui::InputInt("smoothing times", &smoothingTimes))
			{
				smoothingTimes = smoothingTimes > 0 ? smoothingTimes : 0;
//				updatePaintingItems();
				updateFieldsInView(curFrame, false);
			}
			if (ImGui::InputDouble("smoothing ratio", &smoothingRatio))
			{
				smoothingRatio = smoothingRatio > 0 ? smoothingRatio : 0;
//				updatePaintingItems();
				updateFieldsInView(curFrame, false);
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	if (ImGui::CollapsingHeader("Visualization Options", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Checkbox("is show vector fields", &isShowVectorFields))
		{
			updateFieldsInView(curFrame, false);
		}
		if (ImGui::Checkbox("is show wrinkled mesh", &isShowWrinkels))
		{
			updateFieldsInView(curFrame, false);
		}
		if (ImGui::Checkbox("is use v2", &isUseV2))
		{
			updateFieldsInView(curFrame, false);
		}
		if (ImGui::DragFloat("wrinkle amp scaling ratio", &wrinkleAmpScalingRatio, 0.0005, 0, 1))
        {
            if (wrinkleAmpScalingRatio >= 0)
                updateFieldsInView(curFrame, false);
        }
	}

	if (ImGui::CollapsingHeader("Edition Options", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
		if (ImGui::BeginTabBar("Selected Region", tab_bar_flags))
		{
			if (ImGui::BeginTabItem("Local"))
			{
				buildFacesMenu();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Global"))
			{
				ImGui::Checkbox("Select all", &isSelectAll);
				ImGui::Combo("edition motion", (int*)&selectedMotion, "Ratate\0Tilt\0Enlarge\0None\0");
				if (ImGui::InputDouble("motion value", &selectedMotionValue))
				{
					if (selectedMotionValue < 0 && selectedMotion == Enlarge)
						selectedMotionValue = 0;
				}
				ImGui::Checkbox("vec mag coupled", &isCoupled);
				if (ImGui::InputDouble("mag motion value", &selectedMagValue))
				{
					if (selectedMagValue < 0)
						selectedMagValue = 1;
				}
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	

	if (ImGui::CollapsingHeader("Frame Visualization Options", ImGuiTreeNodeFlags_DefaultOpen))
	{
//		if (ImGui::SliderInt("current frame", &curFrame, 0, numFrames - 1))
        if (ImGui::DragInt("current frame", &curFrame, 1, 0, numFrames - 1))
		{
			curFrame = curFrame % (numFrames + 1);
			updateFieldsInView(curFrame, false);
		}
        if (ImGui::SliderInt("current frame slider bar", &curFrame, 0, numFrames - 1))
        {
            curFrame = curFrame % (numFrames + 1);
            updateFieldsInView(curFrame, false);
        }
		if (ImGui::DragFloat("vec ratio", &(vecratio), 0.00005, 0, 1))
		{
			updateFieldsInView(curFrame, false);
		}
	}

	if (ImGui::CollapsingHeader("optimzation parameters", ImGuiTreeNodeFlags_DefaultOpen))
	{
		   /*
     		CWF = 0,
			HalfFullCWF = 1,
			FullCWF = 2,
			LocalCWF = 3,
			LinearCWF = 4,
			KnoppelCWF = 5,
    		*/
		ImGui::Combo("model type", (int*)&editModelType, "CWF\0HalfFullCWF\0FullCWF\0LocalCWF\0LinearCWF\0Knoppel\0");
		if(ImGui::Combo("Initialization type", (int*)&initType, "Linear\0SeperateLinear\0Knoppel\0"))
        {
            std::cout << "init type: " << initType << std::endl;
        }
		if (ImGui::InputDouble("Zuenko Tau", &zuenkoTau))
		{
			if (zuenkoTau < 0)
				zuenkoTau = 0;
		}
		if (ImGui::InputInt("Zuenko Inner Iter", &zuenkoIter))
		{
			if (zuenkoIter < 0)
				zuenkoTau = 5;
		}
		if (ImGui::InputInt("num of frames", &numFrames))
		{
			if (numFrames <= 0)
				numFrames = 10;
		}

		if (ImGui::InputInt("num iterations", &numIter))
		{
			if (numIter < 0)
				numIter = 1000;
		}
		if (ImGui::InputDouble("grad tol", &gradTol))
		{
			if (gradTol < 0)
				gradTol = 1e-6;
		}
		if (ImGui::InputDouble("x tol", &xTol))
		{
			if (xTol < 0)
				xTol = 0;
		}
		if (ImGui::InputDouble("f tol", &fTol))
		{
			if (fTol < 0)
				fTol = 0;
		}
		if (ImGui::InputInt("quad order", &quadOrder))
		{
			if (quadOrder <= 0 || quadOrder > 20)
				quadOrder = 4;
		}

		if (ImGui::InputInt("effective factor", &effectivedistFactor))
		{
			if (effectivedistFactor < 0)
				effectivedistFactor = 4;
		}
		if (ImGui::InputDouble("spatial amp ratio", &spatialAmpRatio))
		{
			if (spatialAmpRatio < 0)
				spatialAmpRatio = 1;
		}

		if (ImGui::InputDouble("spatial edge ratio", &spatialEdgeRatio))
		{
			if (spatialEdgeRatio < 0)
				spatialEdgeRatio = 1;
		}

		if (ImGui::InputDouble("spatial knoppel ratio", &spatialKnoppelRatio))
		{
			if (spatialKnoppelRatio < 0)
				spatialKnoppelRatio = 1;
		}

		ImGui::Checkbox("reinitialize before solve", &isForceReinitilaize);

	}
	if (ImGui::Button("Reinitialization", ImVec2((w - p) / 2.f, 0)))
	{

		updateEditionDomain();
		// solve for the path from source to target
		reinitializeKeyFrames(initZvals, initOmega, faceFlags, omegaList, zList, initType);
		updatePaintingItems();
		updateFieldsInView(curFrame, false);
	}
	ImGui::SameLine();
	if (ImGui::Button("Solve", ImVec2((w - p) / 2.f, 0)))
	{

		updateEditionDomain();
		// solve for the path from source to target
		solveKeyFrames(initZvals, initOmega, faceFlags, omegaList, zList);
		updatePaintingItems();
		updateFieldsInView(curFrame, false);
	}

	if (ImGui::Button("update viewer", ImVec2(-1, 0)))
	{
		updateEditionDomain();
		updatePaintingItems();
		updateFieldsInView(curFrame, false);
	}

	if (ImGui::Button("output images", ImVec2(-1, 0)))
	{
		std::string curFolder = workingFolder + "/screenshots/";
		mkdir(curFolder);
		std::cout << "save folder: " << curFolder << std::endl;
		for (int i = 0; i < ampFieldsList.size(); i++)
		{
			updateFieldsInView(i, false);
			//polyscope::options::screenshotExtension = ".jpg";
			std::string name = curFolder + "/output_" + std::to_string(i) + ".jpg";
			polyscope::screenshot(name);
		}
	}

	if (ImGui::Button("test", ImVec2(-1, 0)))
	{
		for (int i = 0; i < zList.size(); i++)
		{
			double knoppel = editModel->spatialKnoppelEnergy(i);

			double ampDiff = editModel->temporalAmpDifference(i);
			std::cout << "frame: " << i << ", knoppel: " << knoppel << ", amp diff: " << ampDiff;

			if (i < zList.size() - 1)
			{
				double kinetic = editModel->kineticEnergy(i);
				std::cout << ", kinetic: " << kinetic << std::endl;
			}
			else
				std::cout << std::endl;
		}
	}


	ImGui::PopItemWidth();
}


int main(int argc, char** argv)
{
	/*igl::readOBJ("G:/WrinkleEdition_dataset/edgemodel/50frames_new/didactic/Naive/cylinder_local/mesh.obj", testV, testF);
	MeshConnectivity testMesh(testF);
	Eigen::VectorXd edgeW;
	std::vector<std::complex<double>> testz;
	loadEdgeOmega("G:/WrinkleEdition_dataset/edgemodel/50frames_new/didactic/Naive/cylinder_local/omega.txt", testMesh.nEdges(), edgeW);
	loadVertexZvals("G:/WrinkleEdition_dataset/edgemodel/50frames_new/didactic/Naive/cylinder_local/zvals.txt", testV.rows(), testz);*/
	/*for (int i = 0; i < testz.size(); i++)
	{
		testz[i] *= 0.25;
	}
	for (int i = 0; i < edgeW.rows(); i++)
	{
		edgeW(i) *= 4.0;
	}
	saveEdgeOmega("G:/WrinkleEdition_dataset/edgemodel/50frames_new/didactic/Naive/cylinder_local/omega_new.txt", edgeW);
	saveVertexZvals("G:/WrinkleEdition_dataset/edgemodel/50frames_new/didactic/Naive/cylinder_local/zvals_new.txt", testz);*/

	if (!loadProblem())
	{
		std::cout << "failed to load file." << std::endl;
		return 1;
	}


	// Options
	polyscope::options::autocenterStructures = true;
	polyscope::view::windowWidth = 1024;
	polyscope::view::windowHeight = 1024;

	// Initialize polyscope
	polyscope::init();

	// Register the mesh with Polyscope
//    polyscope::registerSurfaceMesh("input mesh", triV, triF);


	polyscope::view::upDir = polyscope::view::UpDir::ZUp;

	// Add the callback
	polyscope::state::userCallback = callback;

	polyscope::options::groundPlaneHeightFactor = 0.25; // adjust the plane height

	updateFieldsInView(curFrame, true);
	// Show the gui
	polyscope::show();


	return 0;
}