/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
// NOTE: It is possible to simply include "elemental.hpp" instead
#include "elemental-lite.hpp"
#include ELEM_FROBENIUSNORM_INC
#include ELEM_PSEUDOSPECTRUM_INC

#include ELEM_FOXLI_INC
#include ELEM_GRCAR_INC
#include ELEM_HELMHOLTZPML_INC
#include ELEM_LOTKIN_INC
#include ELEM_UNIFORM_INC
using namespace std;
using namespace elem;

typedef double Real;
typedef Complex<Real> C;

int
main( int argc, char* argv[] )
{
    Initialize( argc, argv );

    try 
    {
        Int r = Input("--gridHeight","process grid height",0);
        const bool colMajor = Input("--colMajor","column-major ordering?",true);
        const Int matType = 
            Input("--matType","0:uniform,1:Haar,2:Lotkin,3:Grcar,4:FoxLi,"
                              "5:HelmholtzPML1D,6:HelmholtzPML2D",4);
        const Int n = Input("--size","height of matrix",100);
        const Int nbAlg = Input("--nbAlg","algorithmic blocksize",96);
        const Real realCenter = Input("--realCenter","real center",0.);
        const Real imagCenter = Input("--imagCenter","imag center",0.);
        Real realWidth = Input("--realWidth","x width of image",0.);
        Real imagWidth = Input("--imagWidth","y width of image",0.);
        const Real numReal = Input("--numReal","num real chunks",2);
        const Real numImag = Input("--numImag","num imag chunks",2);
        const Int realSize = Input("--realSize","number of x samples",100);
        const Int imagSize = Input("--imagSize","number of y samples",100);
        const bool lanczos = Input("--lanczos","use Lanczos?",true);
        const Int krylovSize = Input("--krylovSize","num Lanczos vectors",10);
        const bool reorthog = Input("--reorthog","reorthog basis?",true);
        const bool deflate = Input("--deflate","deflate converged?",true);
        const Int maxIts = Input("--maxIts","maximum two-norm iter's",1000);
        const Real tol = Input("--tol","tolerance for norm estimates",1e-6);
#ifdef ELEM_HAVE_SCALAPACK
        // QR algorithm options
        const Int nbDist = Input("--nbDist","distribution blocksize",32);
#else
        // Spectral Divide and Conquer options
        const Int cutoff = Input("--cutoff","problem size for QR",256);
        const Int maxInnerIts = Input("--maxInnerIts","SDC limit",2);
        const Int maxOuterIts = Input("--maxOuterIts","SDC limit",10);
        const bool random = Input("--random","Random RRQR in SDC",true);
        const Real signTol = Input("--signTol","Sign tolerance for SDC",1e-9);
        const Real relTol = Input("--relTol","Rel. tol. for SDC",1e-6);
        const Real spreadFactor = Input("--spreadFactor","median pert.",1e-6);
#endif
        const Real uniformRealCenter = 
            Input("--uniformRealCenter","real center of uniform dist",0.);
        const Real uniformImagCenter =
            Input("--uniformImagCenter","imag center of uniform dist",0.);
        const Real uniformRadius =
            Input("--uniformRadius","radius of uniform dist",1.);
        const Int numBands = Input("--numBands","num bands for Grcar",3);
        const Real omega = Input("--omega","frequency for Fox-Li/Helm",16*M_PI);
        const Int mx = Input("--mx","number of x points for HelmholtzPML",30);
        const Int my = Input("--my","number of y points for HelmholtzPML",30);
        const Int numPmlPoints = Input("--numPml","num PML points for Helm",5);
        const double sigma = Input("--sigma","PML amplitude",1.5);
        const double pmlExp = Input("--pmlExp","PML takeoff exponent",3.);
        const bool progress = Input("--progress","print progress?",true);
        const bool display = Input("--display","display matrices?",false);
        const bool write = Input("--write","write matrices?",false);
        const bool saveSchur = Input("--saveSchur","save Schur factor?",true);
        const bool writePseudo = Input("--writePs","write pseudospec.",false);
        const Int numerFormatInt = Input("--numerFormat","numerical format",2);
        const Int imageFormatInt = Input("--imageFormat","image format",8);
        const Int colorMapInt = Input("--colorMap","color map",0);
        ProcessInput();
        PrintInputReport();

        if( r == 0 )
            r = Grid::FindFactor( mpi::Size(mpi::COMM_WORLD) );
        const GridOrder order = ( colMajor ? COLUMN_MAJOR : ROW_MAJOR );
        const Grid g( mpi::COMM_WORLD, r, order );
        SetBlocksize( nbAlg );
        if( numerFormatInt < 1 || numerFormatInt >= FileFormat_MAX )
            LogicError("Invalid numerical format integer, should be in [1,",
                       FileFormat_MAX,")");
        if( imageFormatInt < 1 || imageFormatInt >= FileFormat_MAX )
            LogicError("Invalid image format integer, should be in [1,",
                       FileFormat_MAX,")");

        const FileFormat numerFormat = static_cast<FileFormat>(numerFormatInt);
        const FileFormat imageFormat = static_cast<FileFormat>(imageFormatInt);
        const ColorMap colorMap = static_cast<ColorMap>(colorMapInt);
        SetColorMap( colorMap );
        const C center(realCenter,imagCenter);
        const C uniformCenter(uniformRealCenter,uniformImagCenter);

        std::string matName;
        DistMatrix<C> A(g);
        switch( matType )
        {
        case 0: matName="uniform"; 
                Uniform( A, n, n, uniformCenter, uniformRadius ); break;
        case 1: matName="Haar"; Haar( A, n ); break;
        case 2: matName="Lotkin"; Lotkin( A, n ); break;
        case 3: matName="Grcar"; Grcar( A, n, numBands ); break;
        case 4: matName="FoxLi"; FoxLi( A, n, omega ); break;
        case 5: matName="HelmholtzPML"; 
                HelmholtzPML
                ( A, n, C(omega), numPmlPoints, sigma, pmlExp ); break;
        case 6: matName="HelmholtzPML2D"; 
                HelmholtzPML
                ( A, mx, my, C(omega), numPmlPoints, sigma, pmlExp ); break;
        default: LogicError("Invalid matrix type");
        }
        if( display )
            Display( A, "A" );
        if( write )
        {
            Write( A, "A", numerFormat );
            Write( A, "A", imageFormat );
        }

        // Begin by computing the Schur decomposition
        Timer timer;
        DistMatrix<C,VR,STAR> w(g);
        mpi::Barrier( mpi::COMM_WORLD );
        const bool formATR = true;
#ifdef ELEM_HAVE_SCALAPACK
        SetDefaultBlockHeight( nbDist );
        SetDefaultBlockWidth( nbDist );
        timer.Start();
        schur::QR( A, w, formATR );
        mpi::Barrier( mpi::COMM_WORLD );
        const double qrTime = timer.Stop();
        if( mpi::WorldRank() == 0 )
            std::cout << "QR algorithm took " << qrTime << " seconds" 
                      << std::endl; 
#else
        timer.Start();
        DistMatrix<C> X(g);
        schur::SDC
        ( A, w, X, formATR, cutoff, maxInnerIts, maxOuterIts, signTol, relTol, 
          spreadFactor, random, progress );
        mpi::Barrier( mpi::COMM_WORLD );
        const double sdcTime = timer.Stop();
        if( mpi::WorldRank() == 0 )
            std::cout << "SDC took " << sdcTime << " seconds" << std::endl; 
#endif
        if( saveSchur )
        {
            if( mpi::WorldRank() == 0 )
            {
                std::cout << "Writing Schur decomposition to file...";
                std::cout.flush();
            }
            timer.Start();
            std::ostringstream os;
            os << matName << "-" << A.ColStride() << "x" << A.RowStride()
               << "-" << A.DistRank();
            write::Binary( A.LockedMatrix(), os.str() );
            mpi::Barrier( mpi::COMM_WORLD );
            const double saveSchurTime = timer.Stop();
            if( mpi::WorldRank() == 0 )
                std::cout << "DONE. " << saveSchurTime << " seconds" 
                          << std::endl;
        }

        // Find a window if none is specified
        if( realWidth == 0. || imagWidth == 0. )
        {
            const Real radius = MaxNorm( w );
            const Real oneNorm = OneNorm( A );
            Real width;
            if( oneNorm == 0. && radius == 0. )
            {
                width = 1;
                if( mpi::WorldRank() == 0 )
                    std::cout << "Setting width to 1 to handle zero matrix"
                              << std::endl;
            }
            else if( radius >= 0.2*oneNorm )
            {
                width = 2.5*radius;
                if( mpi::WorldRank() == 0 )
                    std::cout << "Setting width to " << width
                              << " based on the spectral radius, " 
                              << radius << std::endl;
            }
            else
            {
                width = 0.8*oneNorm;
                if( mpi::WorldRank() == 0 )
                    std::cout << "Setting width to " << width
                              << " based on the one norm, " << oneNorm 
                              << std::endl;
            }
            realWidth = width;
            imagWidth = width;
        }

        // Visualize/write the pseudospectrum within each window
        DistMatrix<Real> invNormMap(g);
        DistMatrix<Int> itCountMap(g);
        const Int xBlock = realSize / numReal;
        const Int yBlock = imagSize / numImag;
        const Int xLeftover = realSize - (numReal-1)*xBlock;
        const Int yLeftover = imagSize - (numImag-1)*yBlock;
        const Real realStep = realWidth/realSize;
        const Real imagStep = imagWidth/imagSize;
        const C corner = center - C(realWidth/2,imagWidth/2);
        for( Int realChunk=0; realChunk<numReal; ++realChunk )
        {
            const Int realChunkSize = 
                ( realChunk==numReal-1 ? xLeftover : xBlock );
            const Real realChunkWidth = realStep*realChunkSize;
            for( Int imagChunk=0; imagChunk<numImag; ++imagChunk )
            {
                const Int imagChunkSize = 
                    ( imagChunk==numImag-1 ? yLeftover : yBlock );
                const Real imagChunkWidth = imagStep*imagChunkSize;

                const C chunkCorner = corner + 
                    C(realStep*realChunk*xBlock,imagStep*imagChunk*yBlock);
                const C chunkCenter = chunkCorner + 
                    0.5*C(realStep*realChunkSize,imagStep*imagChunkSize);

                if( mpi::WorldRank() == 0 )
                    std::cout << "Starting computation for chunk centered at "
                              << chunkCenter << std::endl;
                mpi::Barrier( mpi::COMM_WORLD );
                timer.Start();
                itCountMap = TriangularPseudospectrum
                ( A, invNormMap, chunkCenter, realChunkWidth, imagChunkWidth, 
                  realChunkSize, imagChunkSize, lanczos, krylovSize, reorthog, 
                  deflate, maxIts, tol, progress );
                mpi::Barrier( mpi::COMM_WORLD );
                const double pseudoTime = timer.Stop();
                const Int numIts = MaxNorm( itCountMap );
                if( mpi::WorldRank() == 0 )
                {
                    std::cout << "num seconds=" << pseudoTime << "\n"
                              << "num iterations=" << numIts << std::endl;
                }

                std::ostringstream chunkStream;
                chunkStream << "_" << realChunk << "_" << imagChunk;
                const std::string chunkTag = chunkStream.str();
                if( display )
                {
                    Display( invNormMap, "invNormMap"+chunkTag );
                    Display( itCountMap, "itCountMap"+chunkTag );
                }
                if( write || writePseudo )
                {
                    Write( invNormMap, "invNormMap"+chunkTag, numerFormat );
                    Write( invNormMap, "invNormMap"+chunkTag, imageFormat );
                    Write( itCountMap, "itCountMap"+chunkTag, numerFormat );
                    Write( itCountMap, "itCountMap"+chunkTag, imageFormat );
                }

                // Take the element-wise log
                const Int mLocal = invNormMap.LocalHeight();
                const Int nLocal = invNormMap.LocalWidth();
                for( Int jLoc=0; jLoc<nLocal; ++jLoc )
                    for( Int iLoc=0; iLoc<mLocal; ++iLoc )
                        invNormMap.SetLocal
                        ( iLoc, jLoc, Log(invNormMap.GetLocal(iLoc,jLoc)) );
                if( display )
                {
                    Display( invNormMap, "logInvNormMap"+chunkTag );
                    if( GetColorMap() != GRAYSCALE_DISCRETE )
                    {
                        auto colorMap = GetColorMap();
                        SetColorMap( GRAYSCALE_DISCRETE );
                        Display( invNormMap, "discreteLogInvNormMap"+chunkTag );
                        SetColorMap( colorMap );
                    }
                }
                if( write || writePseudo )
                {
                    Write( invNormMap, "logInvNormMap"+chunkTag, numerFormat );
                    Write( invNormMap, "logInvNormMap"+chunkTag, imageFormat );
                    if( GetColorMap() != GRAYSCALE_DISCRETE )
                    {
                        auto colorMap = GetColorMap();
                        SetColorMap( GRAYSCALE_DISCRETE );
                        Write
                        ( invNormMap, "discreteLogInvNormMap"+chunkTag, 
                          numerFormat );
                        Write
                        ( invNormMap, "discreteLogInvNormMap"+chunkTag, 
                          imageFormat );
                        SetColorMap( colorMap );
                    }
                }
            }
        }
    }
    catch( exception& e ) { ReportException(e); }

    Finalize();
    return 0;
}
