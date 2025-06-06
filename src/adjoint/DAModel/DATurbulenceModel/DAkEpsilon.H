/*---------------------------------------------------------------------------*\

    DAFoam  : Discrete Adjoint with OpenFOAM
    Version : v4

    Description:
    Child class for the kEpsilon model

    This file is modified from OpenFOAM's source code
    src/TurbulenceModels/turbulenceModels/RAS/kEpsilon/kEpsilon.H

    OpenFOAM: The Open Source CFD Toolbox

    Copyright (C): 2011-2016 OpenFOAM Foundation

    OpenFOAM License:

        OpenFOAM is free software: you can redistribute it and/or modify it
        under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.
    
        OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
        ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
        FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
        for more details.
    
        You should have received a copy of the GNU General Public License
        along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#ifndef DAkEpsilon_H
#define DAkEpsilon_H

#include "DATurbulenceModel.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

/*---------------------------------------------------------------------------*\
                       Class DAkEpsilon Declaration
\*---------------------------------------------------------------------------*/

class DAkEpsilon
    : public DATurbulenceModel
{

protected:
    /// \name SST parameters
    //@{
    dimensionedScalar Cmu_;
    dimensionedScalar C1_;
    dimensionedScalar C2_;
    dimensionedScalar C3_;
    dimensionedScalar sigmak_;
    dimensionedScalar sigmaEps_;
    //@}

    /// \name SST functions
    //@{
    tmp<fvScalarMatrix> kSource() const;
    tmp<fvScalarMatrix> epsilonSource() const;
    //- Return the effective diffusivity for k
    tmp<volScalarField> DkEff() const;
    //- Return the effective diffusivity for epsilon
    tmp<volScalarField> DepsilonEff() const;
    //- Return the turbulence kinetic energy
    virtual tmp<volScalarField> k() const
    {
        return k_;
    }

    //- Return the turbulence kinetic energy dissipation rate
    virtual tmp<volScalarField> epsilon() const
    {
        return epsilon_;
    }
    //@}

    /// \name Augmented variables for adjoint residuals
    //@{
    volScalarField& epsilon_;
    volScalarField epsilonRes_;
    volScalarField& k_;
    volScalarField kRes_;
    //@}

    /// we need to make the G field a class variable and register it to the mesh.db
    /// such that the omegaWallFunction BC can find it
    autoPtr<volScalarField::Internal> GPtr_;

    /// beta field for field inversion
    volScalarField betaFIK_;
    volScalarField betaFIEpsilon_;

    /// cell-center epsilon values near the wall, this is to fix the issue that the
    /// epsilonWallFunction will try to modify epsilon values for the cells near walls
    /// this will cause issue for FD-based partial derivatives, so here we basically
    /// implement a zeroGradient BC for near wall epsilon.
    scalarList epsilonNearWall_;

    /// whether to solve for turb states
    label solveTurbState_ = 0;

public:
    TypeName("kEpsilon");
    // Constructors

    //- Construct from components
    DAkEpsilon(
        const word modelType,
        const fvMesh& mesh,
        const DAOption& daOption);

    //- Destructor
    virtual ~DAkEpsilon()
    {
    }

    // Member functions

    /// update the turbulence state for DAStateInfo::regStates_
    virtual void correctModelStates(wordList& modelStates) const;

    /// update nut based on other turbulence variables and update the BCs
    virtual void correctNut();

    /// update turbulence variable boundary values
    virtual void correctBoundaryConditions();

    /// update any intermediate variables that are dependent on state variables and are used in calcResiduals
    virtual void updateIntermediateVariables();

    /// update the original variable connectivity for the adjoint state residuals in stateCon
    virtual void correctStateResidualModelCon(List<List<word>>& stateCon) const;

    /// add the model residual connectivity to stateCon
    virtual void addModelResidualCon(HashTable<List<List<word>>>& allCon) const;

    /// compute the turbulence residuals
    virtual void calcResiduals(const dictionary& options);

    /// solve the residual equations and update the state
    virtual void correct(label printToScreen);

    /// save near wall epsilon values to epsilonNearWall_
    void saveEpsilonNearWall();

    /// set epsilonNearWall_ to near wall epsilon values
    void setEpsilonNearWall();

    /// specially treatment to correct epsilon BC
    void correctEpsilonBoundaryConditions();

    /// return the diag(), upper(), and lower() scalarFields from the turbulence model's fvMatrix
    virtual void getFvMatrixFields(
        const word varName,
        scalarField& diag,
        scalarField& upper,
        scalarField& lower);

    /// return the value of the destruction term from the turbulence model
    virtual void getTurbProdOverDestruct(volScalarField& PoD) const;

    /// return the value of the convective over production term from the turbulence model
    virtual void getTurbConvOverProd(volScalarField& CoP) const;
};

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

} // End namespace Foam

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

#endif

// ************************************************************************* //
