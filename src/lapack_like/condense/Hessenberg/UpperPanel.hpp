/*
   Copyright (c) 2009-2016, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License,
   which can be found in the LICENSE file in the root directory, or at
   http://opensource.org/licenses/BSD-2-Clause
*/
#ifndef EL_HESSENBERG_UPPER_PANEL_HPP
#define EL_HESSENBERG_UPPER_PANEL_HPP

namespace El {
namespace hessenberg {

// NOTE: This is an extension into complex arithmetic of the sequential
// algorithm proposed in:
//       G. Quintana-Orti and R. van de Geijn,
//       "Improving the performance of reduction to Hessenberg form"
//       After switching to complex arithmetic, it was more natural to switch
//       to lower-triangular matrices in the UT transform.

// NOTE: It would be possible to avoid the space for U if we were more careful
//       about applying the portion interleaved with the Hessenberg matrix.
template<typename F>
void UpperPanel
( Matrix<F>& A,
  Matrix<F>& householderScalars,
  Matrix<F>& U,
  Matrix<F>& V,
  Matrix<F>& G )
{
    EL_DEBUG_CSE
    const Int nU = U.Width();
    const Int n = A.Height();
    EL_DEBUG_ONLY(
      if( nU >= n )
          LogicError("V is too wide for the panel factorization");
      if( U.Height() != A.Height() )
          LogicError("U must be the same height as A");
      if( V.Height() != A.Height() )
          LogicError("V must be the same height as A");
      if( V.Width() != nU )
          LogicError("V must be the same width as U");
    )
    const Int householderScalarsHeight = Max(nU,0);
    householderScalars.Resize( householderScalarsHeight, 1 );

    Zeros( U, n, nU );
    Zeros( V, n, nU );
    Zeros( G, nU, nU );

    Matrix<F> y10;

    for( Int k=0; k<nU; ++k )
    {
        const Range<Int> ind0( 0,   k   ),
                         ind1( k,   k+1 ),
                         ind2( k+1, n   );

        auto a21 = A( ind2,    ind1 );
        auto a1  = A( IR(0,n), ind1 );
        auto A2  = A( IR(0,n), ind2 );

        auto alpha21T = A( IR(k+1,k+2), ind1 );
        auto a21B     = A( IR(k+2,n),   ind1 );

        auto U0  = U( IR(0,n), ind0 );
        auto u10 = U( ind1,    ind0 );
        auto u21 = U( ind2,    ind1 );
        auto U20 = U( ind2,    ind0 );

        auto V0 = V( IR(0,n), ind0 );
        auto v1 = V( IR(0,n), ind1 );

        auto G00     = G( ind0, ind0 );
        auto g10     = G( ind1, ind0 );
        auto gamma11 = G( ind1, ind1 );

        // a1 := (I - U0 inv(G00) U0^H) (a1 - V0 inv(G00)^H u10^H)
        // -------------------------------------------------------
        // a1 := a1 - V0 inv(G00)^H u10^H
        Conjugate( u10, y10 );
        Trsv( LOWER, ADJOINT, NON_UNIT, G00, y10 );
        Gemv( NORMAL, F(-1), V0, y10, F(1), a1 );
        // a1 := a1 - U0 (inv(G00) (U0^H a1))
        Gemv( ADJOINT, F(1), U0, a1, F(0), y10 );
        Trsv( LOWER, NORMAL, NON_UNIT, G00, y10 );
        Gemv( NORMAL, F(-1), U0, y10, F(1), a1 );

        // Find tau and v such that
        //  / I - tau | 1 | | 1, v^H | \ | alpha21T | = | beta |
        //  \         | v |            / |     a21B |   |    0 |
        const F tau = LeftReflector( alpha21T, a21B );
        householderScalars(k) = tau;

        // Store u21 := | 1 |
        //              | v |
        u21 = a21;
        u21(0) = F(1);

        // v1 := A2 u21
        Gemv( NORMAL, F(1), A2, u21, F(0), v1 );

        // g10 := u21^H U20 = (U20^H u21)^H
        Gemv( ADJOINT, F(1), U20, u21, F(0), g10 );
        Conjugate( g10 );

        // gamma11 := 1/tau
        gamma11(0) = F(1)/tau;
    }
}

template<typename F>
void UpperPanel
( DistMatrix<F>& A,
  DistMatrix<F,STAR,STAR>& householderScalars,
  DistMatrix<F,MC,  STAR>& U_MC_STAR,
  DistMatrix<F,MR,  STAR>& U_MR_STAR,
  DistMatrix<F,MC,  STAR>& V_MC_STAR,
  DistMatrix<F,STAR,STAR>& G_STAR_STAR )
{
    EL_DEBUG_CSE
    const Int nU = U_MC_STAR.Width();
    const Int n = A.Height();
    EL_DEBUG_ONLY(
      AssertSameGrids
      ( A, householderScalars, U_MC_STAR, U_MR_STAR, V_MC_STAR, G_STAR_STAR );
      if( A.ColAlign() != U_MC_STAR.ColAlign() )
          LogicError("A and U[MC,* ] must be aligned");
      if( A.RowAlign() != U_MR_STAR.ColAlign() )
          LogicError("A and U[MR,* ] must be aligned");
      if( A.ColAlign() != V_MC_STAR.ColAlign() )
          LogicError("A and V[MC,* ] must be aligned");
      if( nU >= n )
          LogicError("V is too wide for the panel factorization");
      if( U_MC_STAR.Height() != A.Height() )
          LogicError("U[MC,* ] must be the same height as A");
      if( U_MR_STAR.Height() != A.Height() )
          LogicError("U[MR,* ] must be the same height as A");
      if( U_MR_STAR.Width() != nU )
          LogicError("U[MR,* ] must be the same width as U[MC,* ]");
      if( V_MC_STAR.Height() != A.Height() )
          LogicError("V[MC,* ] must be the same height as A");
      if( V_MC_STAR.Width() != nU )
          LogicError("V[MC,* ] must be the same width as U");
    )
    const Grid& g = A.Grid();

    Zeros( U_MC_STAR,   n,  nU );
    Zeros( U_MR_STAR,   n,  nU );
    Zeros( V_MC_STAR,   n,  nU );
    Zeros( G_STAR_STAR, nU, nU );

    DistMatrix<F,MC,  STAR> a1_MC(g);
    DistMatrix<F,STAR,STAR> y10_STAR(g);

    for( Int k=0; k<nU; ++k )
    {
        const Range<Int> ind0( 0,   k   ),
                         ind1( k,   k+1 ),
                         ind2( k+1, n   );

        auto a21 = A( ind2,    ind1 );
        auto a1  = A( IR(0,n), ind1 );
        auto A2  = A( IR(0,n), ind2 );

        auto alpha21T = A( IR(k+1,k+2), ind1 );
        auto a21B     = A( IR(k+2,n),   ind1 );

        auto U0_MC_STAR  = U_MC_STAR( IR(0,n), ind0 );
        auto u10_MC      = U_MC_STAR( ind1,    ind0 );
        auto u21_MC      = U_MC_STAR( ind2,    ind1 );
        auto u21_MR      = U_MR_STAR( ind2,    ind1 );
        auto U20_MR_STAR = U_MR_STAR( ind2,    ind0 );

        auto V0_MC_STAR = V_MC_STAR( IR(0,n), ind0 );
        auto v1_MC      = V_MC_STAR( IR(0,n), ind1 );

        auto G00_STAR_STAR = G_STAR_STAR( ind0, ind0 );
        auto g10_STAR      = G_STAR_STAR( ind1, ind0 );
        auto gamma11       = G_STAR_STAR( ind1, ind1 );

        // a1 := (I - U0 inv(G00) U0^H) (a1 - V0 inv(G00)^H u10^H)
        // -------------------------------------------------------
        // a1 := a1 - V0 inv(G00)^H u10^H
        a1_MC.AlignWith( a1 );
        a1_MC = a1;
        Conjugate( u10_MC, y10_STAR );
        Trsv
        ( LOWER, ADJOINT, NON_UNIT,
          G00_STAR_STAR.LockedMatrix(), y10_STAR.Matrix() );
        LocalGemv( NORMAL, F(-1), V0_MC_STAR, y10_STAR, F(1), a1_MC );
        // a1 := a1 - U0 (inv(G00) (U0^H a1))
        LocalGemv( ADJOINT, F(1), U0_MC_STAR, a1_MC, F(0), y10_STAR );
        El::AllReduce( y10_STAR, U0_MC_STAR.ColComm() );
        Trsv
        ( LOWER, NORMAL, NON_UNIT,
          G00_STAR_STAR.LockedMatrix(), y10_STAR.Matrix() );
        LocalGemv( NORMAL, F(-1), U0_MC_STAR, y10_STAR, F(1), a1_MC );
        a1 = a1_MC;

        // Find tau and v such that
        //  / I - tau | 1 | | 1, v^H | \ | alpha21T | = | beta |
        //  \         | v |            / |     a21B |   |    0 |
        const F tau = LeftReflector( alpha21T, a21B );
        householderScalars.Set(k,0,tau);

        // Store u21 := | 1 |
        //              | v |
        u21_MC = a21;
        u21_MR = a21;
        u21_MC.Set(0,0,F(1));
        u21_MR.Set(0,0,F(1));

        // v1 := A2 u21
        LocalGemv( NORMAL, F(1), A2, u21_MR, F(0), v1_MC );
        El::AllReduce( v1_MC, A2.RowComm() );

        // g10 := u21^H U20 = (U20^H u21)^H
        LocalGemv
        ( ADJOINT, F(1), U20_MR_STAR, u21_MR, F(0), g10_STAR );
        El::AllReduce( g10_STAR, U20_MR_STAR.ColComm() );
        Conjugate( g10_STAR );

        // gamma11 := 1/tau
        gamma11.Set(0,0,F(1)/tau);
    }
}

} // namespace hessenberg
} // namespace El

#endif // ifndef EL_HESSENBERG_UPPER_PANEL_HPP
