#pragma once
#include <FECore/FECoreBase.h>
#include <FECore/DataRecord.h>
#include "FERigidBody.h"

//-----------------------------------------------------------------------------
//! Base class for object log data (e.g. rigid bodies)
class FECORE_API FELogObjectData : public FECoreBase
{
	DECLARE_SUPER_CLASS(FEOBJLOGDATA_ID);

public:
	FELogObjectData(FEModel* fem) : FECoreBase(fem, FEOBJLOGDATA_ID) {}
	virtual ~FELogObjectData(){}
	virtual double value(FERigidBody& rb) = 0;
};

//-----------------------------------------------------------------------------
class FECORE_API ObjectDataRecord : public DataRecord
{
public:
	ObjectDataRecord(FEModel* pfem, const char* szfile) : DataRecord(pfem, szfile, FE_DATA_RB){}
	double Evaluate(int item, int ndata);
	void Parse(const char* sz);
	void SelectAllItems();
	int Size() { return (int) m_Data.size(); }

private:
	vector<FELogObjectData*>	m_Data;
};