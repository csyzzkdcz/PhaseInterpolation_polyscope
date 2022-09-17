#include "../include/GetSideVertexPhi.h"
#include "../include/MeshLib/MeshUpsampling.h"
#include "../include/InterpolationScheme/SideVertexSchemes.h"
#include "../include/InterpolationScheme/IntrinsicSideVertexSchemes.h"

void getSideVertexPhi(const Eigen::MatrixXd& V, const MeshConnectivity& mesh, const Eigen::VectorXd& edgeOmega, const Eigen::VectorXd& vertPhi, const std::vector<std::pair<int, Eigen::Vector3d>>& bary, Eigen::VectorXd& upPhi, int upLevel, int interpType)
{
	int nUpVerts = bary.size();
	upPhi.resize(nUpVerts);

	for (int i = 0; i < nUpVerts; i++)
	{
		int fid = bary[i].first;
		Eigen::Vector3d bcoord = bary[i].second;
		std::vector<Eigen::Vector3d> tri;
		tri.push_back(V.row(mesh.faceVertex(fid, 0)));
		tri.push_back(V.row(mesh.faceVertex(fid, 1)));
		tri.push_back(V.row(mesh.faceVertex(fid, 2)));

		std::vector<double> triEdgeOmega(3);
		std::vector<double> vertVals(3);
		for (int j = 0; j < 3; j++)
		{
			int eid = mesh.faceEdge(fid, j);
			if (mesh.edgeVertex(eid, 0) == mesh.faceVertex(fid, (j + 2) % 3))
				triEdgeOmega[j] = -edgeOmega[eid];
			else
				triEdgeOmega[j] = edgeOmega[eid];
			vertVals[j] = vertPhi[mesh.faceVertex(fid, j)];
		}


		if (interpType == 0)
		{
			upPhi[i] = intrinsicLinearSideVertexInterpolation<double>(vertVals, bcoord);
		}
		else if (interpType == 1)
		{
			upPhi[i] = intrinsicCubicSideVertexInterpolation<double>(vertVals, triEdgeOmega, tri, bcoord);
		}
		else
		{
			upPhi[i] = intrinsicWojtanSideVertexInterpolation<double>(vertVals, triEdgeOmega, tri, bcoord);
		}
	}
}

void getSideVertexPhi(const Eigen::MatrixXd& V, const MeshConnectivity& mesh, const Eigen::MatrixXd& vertOmega, const Eigen::VectorXd& vertPhi, const std::vector<std::pair<int, Eigen::Vector3d>>& bary, Eigen::VectorXd& upPhi, int upLevel, int interpType)
{
	int nUpVerts = bary.size();
	upPhi.resize(nUpVerts);

	for (int i = 0; i < nUpVerts; i++)
	{
		int fid = bary[i].first;
		Eigen::Vector3d bcoord = bary[i].second;
		std::vector<Eigen::Vector3d> tri;
		tri.push_back(V.row(mesh.faceVertex(fid, 0)));
		tri.push_back(V.row(mesh.faceVertex(fid, 1)));
		tri.push_back(V.row(mesh.faceVertex(fid, 2)));

		std::vector<Eigen::Matrix<double, 3, 1>> triVertGrad(3);
		std::vector<double> vertVals(3);
		for (int j = 0; j < 3; j++)
		{
			vertVals[j] = vertPhi[mesh.faceVertex(fid, j)];
			triVertGrad[j] = vertOmega.row(mesh.faceVertex(fid, j)).transpose();
		}


		if (interpType == 0)
		{
			upPhi[i] = linearSideVertexInterpolation<double>(vertVals, bcoord);
		}
		else if (interpType == 1)
		{
			upPhi[i] = cubicSideVertexInterpolation<double>(vertVals, triVertGrad, tri, bcoord);
		}
		else
		{
			upPhi[i] = WojtanSideVertexInterpolation<double>(vertVals, triVertGrad, tri, bcoord);
		}
	}
}

void getSideVertexPhi(const Eigen::MatrixXd& V, const MeshConnectivity& mesh, const Eigen::VectorXd& edgeOmega, const Eigen::VectorXd& vertPhi, Eigen::MatrixXd& upV, const MeshConnectivity& upMesh, Eigen::VectorXd& upPhi, int upLevel, int interpType)
{
	// use the intrinsic formula
	Eigen::SparseMatrix<double> mat;
	std::vector<int> facemap;
	std::vector<std::pair<int, Eigen::Vector3d>> bary;

	Eigen::MatrixXi upF;
	meshUpSampling(V, mesh.faces(), upV, upF, upLevel, &mat, &facemap, &bary);

	getSideVertexPhi(V, mesh, edgeOmega, vertPhi, bary, upPhi, upLevel, interpType);

}

void getSideVertexPhi(const Eigen::MatrixXd& V, const MeshConnectivity& mesh, const Eigen::MatrixXd& vertOmega, const Eigen::VectorXd& vertPhi, Eigen::MatrixXd& upV, const MeshConnectivity& upMesh, Eigen::VectorXd& upPhi, int upLevel, int interpType )
{
	// use the extrinsic formula
	Eigen::SparseMatrix<double> mat;
	std::vector<int> facemap;
	std::vector<std::pair<int, Eigen::Vector3d>> bary;

	Eigen::MatrixXi upF;
	meshUpSampling(V, mesh.faces(), upV, upF, upLevel, &mat, &facemap, &bary);

	getSideVertexPhi(V, mesh, vertOmega, vertPhi, bary, upPhi, upLevel, interpType);

	
}