#include "stdafx.h"
#include "FEMortarTiedContact.h"
#include "FEStiffnessMatrix.h"
#include "FECore/FEModel.h"
#include "FECore/mortar.h"
#include "FECore/log.h"

//=============================================================================
// FEMortarTiedSurface
//=============================================================================

//-----------------------------------------------------------------------------
FEMortarTiedSurface::FEMortarTiedSurface(FEMesh* pm) : FEContactSurface(pm) {}

//-----------------------------------------------------------------------------
bool FEMortarTiedSurface::Init()
{
	// always intialize base class first!
	if (FEContactSurface::Init() == false) return false;

	// get the number of nodes
	int NN = Nodes();

	// allocate data structures
	m_L.resize(NN, vec3d(0,0,0));
	m_gap.resize(NN, vec3d(0,0,0));

	return true;
}

//-----------------------------------------------------------------------------
void FEMortarTiedSurface::UpdateNodalAreas()
{
	int NN = Nodes();
	int NF = Elements();
	m_A.resize(NN, 0.0);

	for (int i=0; i<NF; ++i)
	{
		FESurfaceElement& el = Element(i);
		double a = FaceArea(el);

		int nn = el.Nodes();
		double fa = a / (double) nn;
		for (int j=0; j<nn; ++j) m_A[el.m_lnode[j]] += fa;
	}

	for (int i=0; i<NN; ++i) m_A[i] = 1.0/m_A[i];
}

//=============================================================================
// FEMortarTiedContact
//=============================================================================

//-----------------------------------------------------------------------------
// Define sliding interface parameters
BEGIN_PARAMETER_LIST(FEMortarTiedContact, FEContactInterface)
	ADD_PARAMETER(m_blaugon      , FE_PARAM_BOOL  , "laugon"       ); 
	ADD_PARAMETER(m_atol         , FE_PARAM_DOUBLE, "tolerance"    );
	ADD_PARAMETER(m_eps          , FE_PARAM_DOUBLE, "penalty"      );
	ADD_PARAMETER(m_naugmin      , FE_PARAM_INT   , "minaug"       );
	ADD_PARAMETER(m_naugmax      , FE_PARAM_INT   , "maxaug"       );
END_PARAMETER_LIST();

//-----------------------------------------------------------------------------
FEMortarTiedContact::FEMortarTiedContact(FEModel* pfem) : FEContactInterface(pfem), m_ss(&pfem->GetMesh()), m_ms(&pfem->GetMesh())
{
}

//-----------------------------------------------------------------------------
bool FEMortarTiedContact::Init()
{
	// initialize surfaces
	if (m_ms.Init() == false) return false;
	if (m_ss.Init() == false) return false;

	// set the integration rule
	m_pT = dynamic_cast<FESurfaceElementTraits*>(FEElementLibrary::GetElementTraits(FE_TRI3G7));

	// allocate sturcture for the integration weights
	int NS = m_ss.Nodes();
	int NM = m_ms.Nodes();
	m_n1.resize(NS,NS);
	m_n2.resize(NS,NM);

	return true;
}

//-----------------------------------------------------------------------------
void FEMortarTiedContact::Activate()
{
	//! don't forget the base class
	FEContactInterface::Activate();

	m_ss.UpdateNodalAreas();

	// update the mortar weights
	// For tied interfaces, this is only done once, during activation
	UpdateMortarWeights();

	// update the nodal gaps
	// (must be done after mortar eights are updated)
	UpdateNodalGaps();
}

//-----------------------------------------------------------------------------
//! build the matrix profile for use in the stiffness matrix
void FEMortarTiedContact::BuildMatrixProfile(FEStiffnessMatrix& K)
{
	// For now we'll assume that each node on the slave side is connected to the master side
	// This is obviously too much, but we'll worry about improving this later
	int NS = m_ss.Nodes();
	int NM = m_ms.Nodes();
	vector<int> LM(3*(NS+NM));
	for (int i=0; i<NS; ++i)
	{
		FENode& ni = m_ss.Node(i);
		LM[3*i  ] = ni.m_ID[0];
		LM[3*i+1] = ni.m_ID[1];
		LM[3*i+2] = ni.m_ID[2];
	}
	for (int i=0; i<NM; ++i)
	{
		FENode& ni = m_ms.Node(i);
		LM[3*NS + 3*i  ] = ni.m_ID[0];
		LM[3*NS + 3*i+1] = ni.m_ID[1];
		LM[3*NS + 3*i+2] = ni.m_ID[2];
	}
	K.build_add(LM);
}

//-----------------------------------------------------------------------------
//! Update the mortar weights
void FEMortarTiedContact::UpdateMortarWeights()
{
	// clear weights
	m_n1.zero();
	m_n2.zero();

	// number of integration points
	const int MAX_INT = 11;
	const int nint = m_pT->nint;
	vector<double>& gw = m_pT->gw;
	vector<double>& gr = m_pT->gr;
	vector<double>& gs = m_pT->gs;

	// calculate the mortar surface
	MortarSurface mortar;
	CalculateMortarSurface(m_ss, m_ms, mortar);

	ExportMortar(mortar, "mortar.stl");

	// These arrays will store the shape function values of the projection points 
	// on the slave and master side when evaluating the integral over a pallet
	double Ns[MAX_INT][4], Nm[MAX_INT][4];

	// loop over the mortar patches
	int NP = mortar.Patches();
	for (int i=0; i<NP; ++i)
	{
		// get the next patch
		Patch& pi = mortar.GetPatch(i);

		// get the facet ID's that generated this patch
		int k = pi.GetSlaveFacetID();
		int l = pi.GetMasterFacetID();

		// get the non-mortar surface element
		FESurfaceElement& se = m_ss.Element(k);
		// get the mortar surface element
		FESurfaceElement& me = m_ms.Element(l);

		// loop over all patch triangles
		int np = pi.Size();
		for (int j=0; j<np; ++j)
		{
			// get the next facet
			Patch::FACET& fj = pi.Facet(j);

			// calculate the patch area
			// (We multiply by two because the sum of the integration weights in FEBio sum up to the area
			// of the triangle in natural coordinates (=0.5)).
			double Area = fj.Area()*2.0;
			if (Area > 1e-15)
			{
				// loop over integration points
				for (int n=0; n<nint; ++n)
				{
					// evaluate the spatial position of the integration point on the patch
					vec3d xp = fj.Position(gr[n], gs[n]);

					// evaluate the integration points on the slave and master surfaces
					// i.e. determine rs, rm
					double r1 = 0, s1 = 0, r2 = 0, s2 = 0;
					vec3d xs = m_ss.ProjectToSurface(se, xp, r1, s1);
					vec3d xm = m_ms.ProjectToSurface(me, xp, r2, s2);

					assert((r1>=0.0)&&(s1>=0)&&(r1+s1<1.0));
					assert((r2>=0.0)&&(s2>=0)&&(r2+s2<1.0));

					// evaluate shape functions
					se.shape_fnc(Ns[n], r1, s1);
					me.shape_fnc(Nm[n], r2, s2);
				}

				// Evaluate the contributions to the integrals
				int ns = se.Nodes();
				int nm = me.Nodes();
				for (int A=0; A<ns; ++A)
				{
					int a = se.m_lnode[A];

					// loop over all the nodes on the slave facet
					for (int B=0; B<ns; ++B)
					{
						double n1 = 0;
						for (int n=0; n<nint; ++n)
						{
							n1 += gw[n]*Ns[n][A]*Ns[n][B];
						}
						n1 *= Area;

						int b = se.m_lnode[B];
						m_n1[a][b] += n1;
					}

					// loop over all the nodes on the master facet
					for (int C = 0; C<nm; ++C)
					{
						double n2 = 0;
						for (int n=0; n<nint; ++n)
						{
							n2 += gw[n]*Ns[n][A]*Nm[n][C];
						}
						n2 *= Area;

						int c = me.m_lnode[C];
						m_n2[a][c] += n2;
					}
				}
			}		
		}
	}

	// Sanity check: sum should add up to contact area
	int NS = m_ss.Nodes();
	int NM = m_ms.Nodes();

	double sum1 = 0.0;
	for (int A=0; A<NS; ++A)
		for (int B=0; B<NS; ++B) sum1 += m_n1[A][B];

	double sum2 = 0.0;
	for (int A=0; A<NS; ++A)
		for (int C=0; C<NM; ++C) sum2 += m_n2[A][C];
}

//-----------------------------------------------------------------------------
//! calculate contact forces
void FEMortarTiedContact::ContactForces(FEGlobalVector& R)
{
	int NS = m_ss.Nodes();
	int NM = m_ms.Nodes();

	// loop over all slave nodes
	for (int A=0; A<NS; ++A)
	{
		double eps = m_eps*m_ss.m_A[A];
		vec3d gA = m_ss.m_gap[A];
		vec3d tA = m_ss.m_L[A] + gA*eps;

		// loop over all slave nodes
		vector<int> en(1);
		vector<int> lm(3);
		vector<double> fe(3);
		for (int B=0; B<NS; ++B)
		{
			FENode& nodeB = m_ss.Node(B);
			en[0] = m_ss.m_node[B];
			lm[0] = nodeB.m_ID[DOF_X];
			lm[1] = nodeB.m_ID[DOF_Y];
			lm[2] = nodeB.m_ID[DOF_Z];

			double nAB = -m_n1[A][B];
			if (nAB != 0.0)
			{
				fe[0] = tA.x*nAB;
				fe[1] = tA.y*nAB;
				fe[2] = tA.z*nAB;

				R.Assemble(en, lm, fe);
			}
		}

		// loop over master side
		for (int C=0; C<NM; ++C)
		{
			FENode& nodeC = m_ms.Node(C);
			en[0] = m_ms.m_node[C];
			lm[0] = nodeC.m_ID[DOF_X];
			lm[1] = nodeC.m_ID[DOF_Y];
			lm[2] = nodeC.m_ID[DOF_Z];

			double nAC = m_n2[A][C];
			if (nAC != 0.0)
			{
				fe[0] = tA.x*nAC;
				fe[1] = tA.y*nAC;
				fe[2] = tA.z*nAC;

				R.Assemble(en, lm, fe);
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! Update the nodal gaps
void FEMortarTiedContact::UpdateNodalGaps()
{
	// reset nodal gaps
	vector<vec3d>& gap = m_ss.m_gap;
	zero(m_ss.m_gap);

	int NS = m_ss.Nodes();
	int NM = m_ms.Nodes();

	// loop over all slave nodes
	for (int A=0; A<NS; ++A)
	{
		// loop over all slave nodes
		for (int B=0; B<NS; ++B)
		{
			FENode& nodeB = m_ss.Node(B);
			vec3d& xB = nodeB.m_rt;
			double nAB = m_n1[A][B];
			gap[A] += xB*nAB;
		}

		// loop over master side
		for (int C=0; C<NM; ++C)
		{
			FENode& nodeC = m_ms.Node(C);
			vec3d& xC = nodeC.m_rt;
			double nAC = m_n2[A][C];
			gap[A] -= xC*nAC;
		}
	}
}

//-----------------------------------------------------------------------------
//! calculate contact stiffness
void FEMortarTiedContact::ContactStiffness(FESolver* psolver)
{
	int NS = m_ss.Nodes();
	int NM = m_ms.Nodes();

	// A. Linearization of the gap function
	vector<int> lmi(3), lmj(3);
	matrix ke(3,3);
	for (int A=0; A<NS; ++A)
	{
		double eps = m_eps*m_ss.m_A[A];

		// loop over all slave nodes
		for (int B=0; B<NS; ++B)
		{
			FENode& nodeB = m_ss.Node(B);
			lmi[0] = nodeB.m_ID[0];
			lmi[1] = nodeB.m_ID[1];
			lmi[2] = nodeB.m_ID[2];

			double nAB = m_n1[A][B]*eps;
			if (nAB != 0.0)
			{
				// loop over slave nodes
				for (int C=0; C<NS; ++C)
				{
					FENode& nodeC = m_ss.Node(C);
					lmj[0] = nodeC.m_ID[0];
					lmj[1] = nodeC.m_ID[1];
					lmj[2] = nodeC.m_ID[2];

					double nAC = m_n1[A][C]*nAB;
					if (nAC != 0.0)
					{
						ke[0][0] = nAC; ke[0][1] = 0.0; ke[0][2] = 0.0;
						ke[1][0] = 0.0; ke[1][1] = nAC; ke[1][2] = 0.0;
						ke[2][0] = 0.0; ke[2][1] = 0.0; ke[2][2] = nAC;

						psolver->AssembleStiffness2(lmi, lmj, ke);
					}
				}

				// loop over master nodes
				for (int C=0; C<NM; ++C)
				{
					FENode& nodeC = m_ms.Node(C);
					lmj[0] = nodeC.m_ID[0];
					lmj[1] = nodeC.m_ID[1];
					lmj[2] = nodeC.m_ID[2];

					double nAC = -m_n2[A][C]*nAB;
					if (nAC != 0.0)
					{
						ke[0][0] = nAC; ke[0][1] = 0.0; ke[0][2] = 0.0;
						ke[1][0] = 0.0; ke[1][1] = nAC; ke[1][2] = 0.0;
						ke[2][0] = 0.0; ke[2][1] = 0.0; ke[2][2] = nAC;

						psolver->AssembleStiffness2(lmi, lmj, ke);
					}
				}
			}
		}

		// loop over all master nodes
		for (int B=0; B<NM; ++B)
		{
			FENode& nodeB = m_ms.Node(B);
			lmi[0] = nodeB.m_ID[0];
			lmi[1] = nodeB.m_ID[1];
			lmi[2] = nodeB.m_ID[2];

			double nAB = -m_n2[A][B]*eps;
			if (nAB != 0.0)
			{
				// loop over slave nodes
				for (int C=0; C<NS; ++C)
				{
					FENode& nodeC = m_ss.Node(C);
					lmj[0] = nodeC.m_ID[0];
					lmj[1] = nodeC.m_ID[1];
					lmj[2] = nodeC.m_ID[2];

					double nAC = m_n1[A][C]*nAB;
					if (nAC != 0.0)
					{
						ke[0][0] = nAC; ke[0][1] = 0.0; ke[0][2] = 0.0;
						ke[1][0] = 0.0; ke[1][1] = nAC; ke[1][2] = 0.0;
						ke[2][0] = 0.0; ke[2][1] = 0.0; ke[2][2] = nAC;

						psolver->AssembleStiffness2(lmi, lmj, ke);
					}
				}

				// loop over master nodes
				for (int C=0; C<NM; ++C)
				{
					FENode& nodeC = m_ms.Node(C);
					lmj[0] = nodeC.m_ID[0];
					lmj[1] = nodeC.m_ID[1];
					lmj[2] = nodeC.m_ID[2];

					double nAC = -m_n2[A][C]*nAB;
					if (nAC != 0.0)
					{
						ke[0][0] = nAC; ke[0][1] = 0.0; ke[0][2] = 0.0;
						ke[1][0] = 0.0; ke[1][1] = nAC; ke[1][2] = 0.0;
						ke[2][0] = 0.0; ke[2][1] = 0.0; ke[2][2] = nAC;

						psolver->AssembleStiffness2(lmi, lmj, ke);
					}
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
//! calculate Lagrangian augmentations
bool FEMortarTiedContact::Augment(int naug)
{
	if (m_blaugon == false) return true;

	double max_err = 0.0;
	int NS = m_ss.Nodes();
	// loop over all slave nodes
	for (int A=0; A<NS; ++A)
	{
		double eps = m_eps*m_ss.m_A[A];
		vec3d gA = m_ss.m_gap[A];
		vec3d Lold = m_ss.m_L[A];
		vec3d Lnew = Lold + gA*eps;

		double uold = Lold.norm();
		double unew = Lnew.norm();

		double err = fabs((uold - unew)/(uold + unew));
		if (err > max_err) max_err = err;
	}

	bool bconv = true;
	if ((m_atol > 0) && (max_err > m_atol)) bconv = false;
	if (m_naugmin > naug) bconv = false;
	if (m_naugmax <= naug) bconv = true;

	felog.printf(" mortar interface # %d\n", GetID());
	felog.printf("                        CURRENT        REQUIRED\n");
	felog.printf("    normal force : %15le", max_err);
	if (m_atol > 0) felog.printf("%15le\n", m_atol); else felog.printf("       ***\n");

	if (bconv == false)
	{
		// loop over all slave nodes
		for (int A=0; A<NS; ++A)
		{
			double eps = m_eps*m_ss.m_A[A];
			vec3d gA = m_ss.m_gap[A];
			vec3d Lold = m_ss.m_L[A];
			vec3d Lnew = Lold + gA*eps;
			m_ss.m_L[A] = Lnew;
		}
	}

	return bconv;
}

//-----------------------------------------------------------------------------
//! update interface data
void FEMortarTiedContact::Update(int niter)
{
	UpdateNodalGaps();
}

//-----------------------------------------------------------------------------
//! serialize data to archive
void FEMortarTiedContact::Serialize(DumpFile& ar)
{
}

//-----------------------------------------------------------------------------
//! shallow copy
void FEMortarTiedContact::ShallowCopy(DumpStream& dmp, bool bsave)
{
}
