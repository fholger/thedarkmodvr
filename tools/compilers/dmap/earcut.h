#pragma once

#include "containers/BinHeap.h"

/*
 * Triangulation of polygonal face, possibly with hole loops.
 * Algorithm described by David Eberly here:
 *   https://www.geometrictools.com/Documentation/TriangulationByEarClipping.pdf
 */
class EarCutter {
public:
	struct SeamEdge {
		int ids[2];
	};
	struct Triangle {
		int ids[3];
	};

private:
	struct Vertex {
		idVec2 pos;
		//the number returned by AddVertex (assigned sequentally)
		int id;
		//circular double-linked list (indices in "verts")
		int prev, next;
		//index in "reflexIds" or -1 if not reflex
		//note: 180 degrees is reflex
		int inReflex;
		//is this an ear?
		bool isEar;
	};
	struct Loop {
		//range in verts[]
		int beg, end;
		//index of rightmost vertex
		int rightmost;
		//inner/outer classification
		double area;
		int isHole;
	};
	//array of polygonal loops
	idList<Loop> loops;
	//true if FinishLoop was not called after last AddVertex
	bool loopBeingAdded;
	//all vertices as given by caller (never moved)
	idList<Vertex> verts;
	//index of some vertex
	int first = -1;
	//true when total oriented area is negative
	bool reversed = false;
	//array of all reflex vertices (index in "verts")
	idList<int> reflexIds;
	//array of all ear vertices (index in "verts")
	idBinHeap<float> earIds;

	//output seam edges (connecting outer loop and holes to each other)
	idList<SeamEdge> seams;
	//output triangles
	idList<Triangle> tris;

	//temporary memory
	idList<int> _work1, _work2;
	idList<Vertex> _work3;

public:
	//vertices must be added one-by-one in sequental order (traversing along polygon edges)
	//note: last point is automatically joined to the first one
	int AddVertex(const idVec2 &pos);

	//call this to separate polygon loops from each other (if there are holes)
	//note: holes must have orientation opposite to the outer loop
	void FinishLoop(bool *isHole = nullptr);

	//execute algorithm
	void Triangulate();

	//the last ear is special: it is the last remaining triangle
	//if you take all tris except the last one, their i2->i0 edges will generated polygon diagonals
	const idList<Triangle> &GetTriangles() const { return tris; }
	const idList<SeamEdge> &GetSeamEdges() const { return seams; }

	//reset to default-constructed state, but retaining memory buffers
	void Reset();

	//total number of fails in the algorithm (for testing/debugging)
	static int FailsCount;

private:
	void DetectOrientation();
	void ConnectHoles();
	void CutEars();
	idVec2 EstimateErrorZone() const;

	double CrossVectors(int sA, int eA, int sB, int eB) const;
	int OrientVectors(int sA, int eA, int sB, int eB) const;
	bool OutsideTriangle(int a, int b, int c, int p) const;

	bool UpdateReflex(int i);
	bool UpdateEar(int i);

	void RemoveReflex(int i);
	void RemoveEar(int i);
};
