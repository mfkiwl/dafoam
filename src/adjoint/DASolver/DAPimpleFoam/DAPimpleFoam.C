/*---------------------------------------------------------------------------*\

    DAFoam  : Discrete Adjoint with OpenFOAM
    Version : v4

    This class is modified from OpenFOAM's source code
    applications/solvers/incompressible/pimpleFoam

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

#include "DAPimpleFoam.H"

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{

defineTypeNameAndDebug(DAPimpleFoam, 0);
addToRunTimeSelectionTable(DASolver, DAPimpleFoam, dictionary);
// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

DAPimpleFoam::DAPimpleFoam(
    char* argsAll,
    PyObject* pyOptions)
    : DASolver(argsAll, pyOptions),
      pimplePtr_(nullptr),
      pPtr_(nullptr),
      UPtr_(nullptr),
      phiPtr_(nullptr),
      laminarTransportPtr_(nullptr),
      turbulencePtr_(nullptr),
      daTurbulenceModelPtr_(nullptr),
      daFvSourcePtr_(nullptr),
      fvSourcePtr_(nullptr),
      PrPtr_(nullptr),
      PrtPtr_(nullptr),
      TPtr_(nullptr),
      alphatPtr_(nullptr)
{
    // check whether the temperature field exists in the 0 folder
    hasTField_ = DAUtility::isFieldReadable(meshPtr_(), "T", "volScalarField");
}
// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

void DAPimpleFoam::initSolver()
{
    /*
    Description:
        Initialize variables for DASolver
    */

    Info << "Initializing fields for DAPimpleFoam" << endl;
    Time& runTime = runTimePtr_();
    fvMesh& mesh = meshPtr_();
#include "createPimpleControlPython.H"
#include "createFieldsPimple.H"

    // read the RAS model from constant/turbulenceProperties
    const word turbModelName(
        IOdictionary(
            IOobject(
                "turbulenceProperties",
                mesh.time().constant(),
                mesh,
                IOobject::MUST_READ,
                IOobject::NO_WRITE,
                false))
            .subDict("RAS")
            .lookup("RASModel"));
    daTurbulenceModelPtr_.reset(DATurbulenceModel::New(turbModelName, mesh, daOptionPtr_()));

#include "createAdjoint.H"

    // initialize fvSource and the source term
    const dictionary& allOptions = daOptionPtr_->getAllOptions();
    if (allOptions.subDict("fvSource").toc().size() != 0)
    {
        hasFvSource_ = 1;
        Info << "Computing fvSource" << endl;
        word sourceName = allOptions.subDict("fvSource").toc()[0];
        word fvSourceType = allOptions.subDict("fvSource").subDict(sourceName).getWord("type");
        daFvSourcePtr_.reset(DAFvSource::New(
            fvSourceType, mesh, daOptionPtr_(), daModelPtr_(), daIndexPtr_()));
        daFvSourcePtr_->calcFvSource(fvSource);
    }

    // reduceIO does not write mesh, but if there is a shape variable, set writeMesh to 1
    dictionary dvSubDict = daOptionPtr_->getAllOptions().subDict("inputInfo");
    forAll(dvSubDict.toc(), idxI)
    {
        word dvName = dvSubDict.toc()[idxI];
        if (dvSubDict.subDict(dvName).getWord("type") == "volCoord")
        {
            reduceIOWriteMesh_ = 1;
            break;
        }
    }
}

label DAPimpleFoam::solvePrimal()
{
    /*
    Description:
        Call the primal solver to get converged state variables
    */

#include "createRefsPimple.H"

    // call correctNut, this is equivalent to turbulence->validate();
    daTurbulenceModelPtr_->updateIntermediateVariables();

    Info << "\nStarting time loop\n"
         << endl;

    label pimplePrintToScreen = 0;

    // we need to reduce the number of files written to the disk to minimize the file IO load
    label reduceIO = daOptionPtr_->getAllOptions().subDict("unsteadyAdjoint").getLabel("reduceIO");
    wordList additionalOutput;
    if (reduceIO)
    {
        daOptionPtr_->getAllOptions().subDict("unsteadyAdjoint").readEntry<wordList>("additionalOutput", additionalOutput);
    }

    scalar endTime = runTime.endTime().value();
    scalar deltaT = runTime.deltaT().value();
    label nInstances = round(endTime / deltaT);

    // main loop
    label regModelFail = 0;
    label fail = 0;
    for (label iter = 1; iter <= nInstances; iter++)
    {
        ++runTime;

        // if we have unsteadyField in inputInfo, assign GlobalVar::inputFieldUnsteady to OF fields at each time step
        this->updateInputFieldUnsteady();

        printToScreen_ = this->isPrintTime(runTime, printIntervalUnsteady_);

        if (printToScreen_)
        {
            Info << "Time = " << runTime.timeName() << nl << endl;
#include "CourantNo.H"
        }

        // --- Pressure-velocity PIMPLE corrector loop
        while (pimple.loop())
        {
            if (pimple.finalIter() && printToScreen_)
            {
                pimplePrintToScreen = 1;
            }
            else
            {
                pimplePrintToScreen = 0;
            }

#include "UEqnPimple.H"

            // --- Pressure corrector loop
            while (pimple.correct())
            {
#include "pEqnPimple.H"
            }

            if (hasTField_)
            {
#include "TEqnPimple.H"
            }

            laminarTransport.correct();
            daTurbulenceModelPtr_->correct(pimplePrintToScreen);

            // update the output field value at each iteration, if the regression model is active
            fail = daRegressionPtr_->compute();
        }

        regModelFail += fail;

        if (this->validateStates())
        {
            // write data to files and quit
            runTime.writeNow();
            mesh.write();
            return 1;
        }

        this->calcAllFunctions(printToScreen_);
        daRegressionPtr_->printInputInfo(printToScreen_);
        daTurbulenceModelPtr_->printYPlus(printToScreen_);
        this->printElapsedTime(runTime, printToScreen_);

        if (reduceIO && iter < nInstances)
        {
            this->writeAdjStates(reduceIOWriteMesh_, additionalOutput);
            daRegressionPtr_->writeFeatures();
        }
        else
        {
            runTime.write();
            daRegressionPtr_->writeFeatures();
        }
    }

    if (regModelFail != 0)
    {
        return 1;
    }

    // need to save primalFinalTimeIndex_.
    primalFinalTimeIndex_ = runTime.timeIndex();

    // write the mesh to files
    mesh.write();

    Info << "End\n"
         << endl;

    return 0;
}

} // End namespace Foam

// ************************************************************************* //
