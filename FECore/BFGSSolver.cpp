#include "stdafx.h"
#include "BFGSSolver.h"
#include "FESolver.h"
#include "FEException.h"

//-----------------------------------------------------------------------------
// BFGSSolver
//-----------------------------------------------------------------------------

BFGSSolver::BFGSSolver(FENewtonSolver* pns) : FENewtonStrategy(pns)
{
	m_maxups = 10;
	m_cmax   = 1e5;

	// pointer to linear solver
	m_plinsolve = 0;
}

//-----------------------------------------------------------------------------
// Initialization method
void BFGSSolver::Init(int neq, LinearSolver* pls)
{
	if (m_max_buf_size <= 0) m_max_buf_size = m_maxups;

	// allocate storage for BFGS update vectors
	m_V.resize(m_max_buf_size, neq);
	m_W.resize(m_max_buf_size, neq);

	m_D.resize(neq);
	m_G.resize(neq);
	m_H.resize(neq);

	m_neq = neq;
	m_nups = 0;

	m_plinsolve = pls;
}

//-----------------------------------------------------------------------------
//! This function performs a BFGS stiffness update.
//! The last line search step is input to this function.
//! This function performs the update assuming the stiffness matrix
//! is positive definite. In the case that this is not the case
//! the function returns false. The function also returns false if 
//! the condition number is too big. A too big condition number might
//! be an indication of an ill-conditioned matrix and the update should
//! not be performed.

bool BFGSSolver::Update(double s, vector<double>& ui, vector<double>& R0, vector<double>& R1)
{
	// calculate the BFGS update vectors
	int neq = (int)ui.size();
	for (int i = 0; i<neq; ++i)
	{
		m_D[i] = s*ui[i];
		m_G[i] = R0[i] - R1[i];
		m_H[i] = R0[i]*s;
	}

	double dg = m_D*m_G;
	double dh = m_D*m_H;
	double dgi = 1.0 / dg;
	double r = dg / dh;

	// check to see if this is still a pos definite update
//	if (r <= 0) 
//	{
//		return false;
//	}

	// calculate the condition number
//	c = sqrt(r);
	double c = sqrt(fabs(r));

	// make sure c is less than the the maximum.
	if (c > m_cmax) return false;

	// get the correct buffer to fill
	int n = (m_nups % m_max_buf_size);

	// do the update only when allowed
	if ((m_nups < m_max_buf_size) || (m_cycle_buffer == true))
	{
		double* vn = m_V[n];
		double* wn = m_W[n];

		for (int i=0; i<neq; ++i)	
		{
			vn[i] = -m_H[i]*c - m_G[i];
			wn[i] = m_D[i]*dgi;
		}
	}

	// increment update counter
	++m_nups;

	return true;
}

//-----------------------------------------------------------------------------
// This function solves a system of equations using the BFGS update vectors
// The variable m_nups keeps track of how many updates have been made so far.
// 

void BFGSSolver::SolveEquations(vector<double>& x, vector<double>& b)
{
	// get the nr of equations
	int neq = (int)x.size();

	// make sure we need to do work
	if (neq==0) return;

	// create temporary storage
	static vector<double> tmp;
	tmp = b;

	// number of updates can be larger than buffer size, so clamp it
	int nups = (m_nups> m_max_buf_size ? m_max_buf_size : m_nups);

	// get the "0" buffer index
	int n0 = 0;
	if ((m_nups > m_max_buf_size) && (m_cycle_buffer == true))
	{
		n0 = m_nups % m_max_buf_size;
	}

	// loop over all update vectors
	for (int i=nups-1; i>=0; --i)
	{
		int n = (n0 + i) % m_max_buf_size;

		double* vi = m_V[n];
		double* wi = m_W[n];

		double wr = 0;
		for (int j = 0; j<neq; j++) wr += wi[j] * tmp[j];

		for (int j = 0; j<neq; j++) tmp[j] += vi[j] * wr;
	}

	// perform a backsubstitution
	if (m_plinsolve->BackSolve(x, tmp) == false)
	{
		throw LinearSolverFailed();
	}

	// loop again over all update vectors
	for (int i = 0; i<nups; ++i)
	{
		int n = (n0 + i) % m_max_buf_size;

		double* vi = m_V[n];
		double* wi = m_W[n];

		double vr = 0;
		for (int j = 0; j<neq; ++j) vr += vi[j] * x[j];

		for (int j = 0; j<neq; ++j) x[j] += wi[j] * vr;
	}
}
