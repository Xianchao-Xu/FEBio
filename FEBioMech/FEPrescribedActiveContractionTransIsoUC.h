//
//  FEPrescribedActiveContractionTransIsoUC.h
//  FEBioMech
//
//  Created by Gerard Ateshian on 3/13/15.
//  Copyright (c) 2015 febio.org. All rights reserved.
//

#ifndef __FEBioMech__FEPrescribedActiveContractionTransIsoUC__
#define __FEBioMech__FEPrescribedActiveContractionTransIsoUC__

#include "FEUncoupledMaterial.h"

//-----------------------------------------------------------------------------
// This material implements an active contraction model which can be used
// as a component of an uncoupled solid mixture material.
class FEPrescribedActiveContractionTransIsoUC : public FEUncoupledMaterial
{
public:
    //! constructor
    FEPrescribedActiveContractionTransIsoUC(FEModel* pfem);
    
    //! Initialization
    void Init();
    
    //! stress
    mat3ds DevStress(FEMaterialPoint& pt);
    
    //! tangent
    tens4ds DevTangent(FEMaterialPoint& pt);
    
public:
    double	m_T0;       // prescribed active stress
    double	m_thd;		// theta angle for fiber orientation (local coordinates system)
    double	m_phd;		// phi angle for fiber orientation (local coordinates system)
    vec3d	m_n0;		// unit vector along fiber direction (local coordinate system)
    
    DECLARE_PARAMETER_LIST();
};


#endif /* defined(__FEBioMech__FEPrescribedActiveContractionTransIsoUC__) */