/*
   Copyright (c) 2009-2011, Jack Poulson
   All rights reserved.

   This file is part of Elemental.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

    - Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

    - Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    - Neither the name of the owner nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/
#include "elemental/basic_internal.hpp"
#include "elemental/advanced_internal.hpp"
using namespace elemental;
using namespace std;

#include "./UTUtil.hpp"

template<typename R> // representation of a real number
void
elemental::advanced::internal::ApplyPackedReflectorsRUHF
( int offset, 
  const DistMatrix<R,MC,MR>& H,
        DistMatrix<R,MC,MR>& A )
{
#ifndef RELEASE
    PushCallStack("advanced::internal::ApplyPackedReflectorsRUHF");
    if( H.Grid() != A.Grid() )
        throw logic_error("H and A must be distributed over the same grid.");
    if( offset > H.Width() )
        throw logic_error("Transforms out of bounds");
    if( offset < 0 )
        throw logic_error("Transforms cannot extend below matrix.");
    if( H.Width() != A.Width() )
        throw logic_error
              ("Length of transforms must equal width of target matrix.");
#endif
    const Grid& g = H.Grid();

    // Matrix views
    DistMatrix<R,MC,MR>
        HTL(g), HTR(g),  H00(g), H01(g), H02(g),  HPan(g), HPanCopy(g),
        HBL(g), HBR(g),  H10(g), H11(g), H12(g), 
                         H20(g), H21(g), H22(g);
    DistMatrix<R,MC,MR>
        AL(g), AR(g),
        A0(g), A1(g), A2(g);

    DistMatrix<R,VC,  STAR> HPan_STAR_VR(g);
    DistMatrix<R,STAR,MR  > HPan_STAR_MR(g);
    DistMatrix<R,STAR,STAR> SInv_STAR_STAR(g);
    DistMatrix<R,MC,  STAR> Z_MC_STAR(g);
    DistMatrix<R,VC,  STAR> Z_VC_STAR(g);

    LockedPartitionDownDiagonal
    ( H, HTL, HTR,
         HBL, HBR, 0 );
    while( HTL.Height() < H.Height() && HTL.Width() < H.Width() )
    {
        LockedRepartitionDownDiagonal
        ( HTL,  /**/ HTR,  H00, /**/ H01, H02,
         /**************/ /******************/
                /**/       H10, /**/ H11, H12,
          HBL,  /**/ HBR,  H20, /**/ H21, H22 );

        RepartitionRight
        ( AL, /**/ AR,
          A0, /**/ A1, A2 );

        const int HPanWidth = H11.Width() + H12.Width();
        const int HPanHeight = min( H11.Height(), max(HPanWidth-offset,0) );
        HPan.LockedView( H, H00.Height(), H00.Width(), HPanHeight, HPanWidth );

        HPan_STAR_MR.AlignWith( AR );
        Z_MC_STAR.AlignWith( AR );
        Z_VC_STAR.AlignWith( AR );
        Z_MC_STAR.ResizeTo( AR.Height(), HPanHeight );
        SInv_STAR_STAR.ResizeTo( HPanHeight, HPanHeight );
        //--------------------------------------------------------------------//
        HPanCopy = HPan;
        HPanCopy.MakeTrapezoidal( LEFT, UPPER, offset );
        SetDiagonalToOne( LEFT, offset, HPanCopy );

        HPan_STAR_VR = HPanCopy;
        basic::Syrk
        ( UPPER, NORMAL,
          (R)1, HPan_STAR_VR.LockedLocalMatrix(),
          (R)0, SInv_STAR_STAR.LocalMatrix() );
        SInv_STAR_STAR.SumOverGrid();
        HalveMainDiagonal( SInv_STAR_STAR );

        HPan_STAR_MR = HPan_STAR_VR;
        basic::internal::LocalGemm
        ( NORMAL, TRANSPOSE,
          (R)1, AR, HPan_STAR_MR, (R)0, Z_MC_STAR );
        Z_VC_STAR.SumScatterFrom( Z_MC_STAR );

        basic::internal::LocalTrsm
        ( RIGHT, UPPER, NORMAL, NON_UNIT,
          (R)1, SInv_STAR_STAR, Z_VC_STAR );

        Z_MC_STAR = Z_VC_STAR;
        basic::internal::LocalGemm
        ( NORMAL, NORMAL,
          (R)-1, Z_MC_STAR, HPan_STAR_MR, (R)1, AR );
        //--------------------------------------------------------------------//
        HPan_STAR_MR.FreeAlignments();
        Z_MC_STAR.FreeAlignments();
        Z_VC_STAR.FreeAlignments();

        SlideLockedPartitionDownDiagonal
        ( HTL, /**/ HTR,  H00, H01, /**/ H02,
               /**/       H10, H11, /**/ H12,
         /*************/ /******************/
          HBL, /**/ HBR,  H20, H21, /**/ H22 );

        SlidePartitionRight
        ( AL,     /**/ AR,
          A0, A1, /**/ A2 );
    }
#ifndef RELEASE
    PopCallStack();
#endif
}

#ifndef WITHOUT_COMPLEX
template<typename R> // representation of a real number
void
elemental::advanced::internal::ApplyPackedReflectorsRUHF
( Conjugation conjugation, int offset, 
  const DistMatrix<complex<R>,MC,MR  >& H,
  const DistMatrix<complex<R>,MD,STAR>& t,
        DistMatrix<complex<R>,MC,MR  >& A )
{
#ifndef RELEASE
    PushCallStack("advanced::internal::ApplyPackedReflectorsRUHF");
    if( H.Grid() != t.Grid() || t.Grid() != A.Grid() )
        throw logic_error
              ("H, t, and A must be distributed over the same grid.");
    if( offset < 0 )
        throw logic_error("Transforms cannot extend below matrix.");
    if( offset > H.Width() )
        throw logic_error("Transform offset is out of bounds");
    if( H.Width() != A.Width() )
        throw logic_error
              ("Length of transforms must equal width of target matrix");
    if( t.Height() != H.DiagonalLength( offset ) )
        throw logic_error("t must be the same length as H's 'offset' diag");
    if( !t.AlignedWithDiag( H, offset ) )
        throw logic_error("t must be aligned with H's 'offset' diagonal");
#endif
    typedef complex<R> C;
    const Grid& g = H.Grid();

    // Matrix views    
    DistMatrix<C,MC,MR>
        HTL(g), HTR(g),  H00(g), H01(g), H02(g),  HPan(g), HPanCopy(g),
        HBL(g), HBR(g),  H10(g), H11(g), H12(g),
                         H20(g), H21(g), H22(g);
    DistMatrix<C,MC,MR>
        AL(g), AR(g),
        A0(g), A1(g), A2(g);
    DistMatrix<C,MD,STAR>
        tT(g),  t0(g),
        tB(g),  t1(g),
                t2(g);

    DistMatrix<C,STAR,VR  > HPan_STAR_VR(g);
    DistMatrix<C,STAR,MR  > HPan_STAR_MR(g);
    DistMatrix<C,STAR,STAR> t1_STAR_STAR(g);
    DistMatrix<C,STAR,STAR> SInv_STAR_STAR(g);
    DistMatrix<C,MC,  STAR> Z_MC_STAR(g);
    DistMatrix<C,VC,  STAR> Z_VC_STAR(g);

    LockedPartitionDownDiagonal
    ( H, HTL, HTR,
         HBL, HBR, 0 );
    LockedPartitionDown
    ( t, tT,
         tB, 0 );
    PartitionRight
    ( A, AL, AR, 0 );
    while( HTL.Height() < H.Height() && HTL.Width() < H.Width() )
    {
        LockedRepartitionDownDiagonal
        ( HTL, /**/ HTR,  H00, /**/ H01, H02,
         /*************/ /******************/
               /**/       H10, /**/ H11, H12,
          HBL, /**/ HBR,  H20, /**/ H21, H22 );

        const int HPanWidth = H11.Width() + H12.Width();
        const int HPanHeight = min( H11.Height(), max(HPanWidth-offset,0) );
        HPan.LockedView( H, H00.Height(), H00.Width(), HPanHeight, HPanWidth );

        LockedRepartitionDown
        ( tT,  t0,
         /**/ /**/
               t1,
          tB,  t2, HPanHeight );

        RepartitionRight
        ( AL, /**/ AR,
          A0, /**/ A1, A2 );

        HPan_STAR_MR.AlignWith( AR );
        Z_MC_STAR.AlignWith( AR );
        Z_VC_STAR.AlignWith( AR );
        Z_MC_STAR.ResizeTo( AR.Height(), HPanHeight );
        SInv_STAR_STAR.ResizeTo( HPanHeight, HPanHeight );
        //--------------------------------------------------------------------//
        HPanCopy = HPan;
        HPanCopy.MakeTrapezoidal( LEFT, UPPER, offset );
        SetDiagonalToOne( LEFT, offset, HPanCopy );

        HPan_STAR_VR = HPanCopy;
        basic::Herk
        ( UPPER, NORMAL,
          (C)1, HPan_STAR_VR.LockedLocalMatrix(),
          (C)0, SInv_STAR_STAR.LocalMatrix() );
        SInv_STAR_STAR.SumOverGrid();
        t1_STAR_STAR = t1;
        FixDiagonal( conjugation, t1_STAR_STAR, SInv_STAR_STAR );

        basic::Conj( HPan_STAR_VR );
        HPan_STAR_MR = HPan_STAR_VR;
        basic::internal::LocalGemm
        ( NORMAL, ADJOINT, (C)1, AR, HPan_STAR_MR, (C)0, Z_MC_STAR );
        Z_VC_STAR.SumScatterFrom( Z_MC_STAR );

        basic::internal::LocalTrsm
        ( RIGHT, UPPER, NORMAL, NON_UNIT,
          (C)1, SInv_STAR_STAR, Z_VC_STAR );

        Z_MC_STAR = Z_VC_STAR;
        basic::internal::LocalGemm
        ( NORMAL, NORMAL, (C)-1, Z_MC_STAR, HPan_STAR_MR, (C)1, AR );
        //--------------------------------------------------------------------//
        HPan_STAR_MR.FreeAlignments();
        Z_MC_STAR.FreeAlignments();
        Z_VC_STAR.FreeAlignments();

        SlideLockedPartitionDownDiagonal
        ( HTL, /**/ HTR,  H00, H01, /**/ H02,
               /**/       H10, H11, /**/ H12,
         /*************/ /******************/
          HBL, /**/ HBR,  H20, H21, /**/ H22 );

        SlideLockedPartitionDown
        ( tT,  t0,
               t1,
         /**/ /**/
          tB,  t2 );

        SlidePartitionRight
        ( AL,     /**/ AR,
          A0, A1, /**/ A2 );
    }
#ifndef RELEASE
    PopCallStack();
#endif
}
#endif

template void elemental::advanced::internal::ApplyPackedReflectorsRUHF
( int offset,
  const DistMatrix<float,MC,MR>& H,
        DistMatrix<float,MC,MR>& A );

template void elemental::advanced::internal::ApplyPackedReflectorsRUHF
( int offset,
  const DistMatrix<double,MC,MR>& H,
        DistMatrix<double,MC,MR>& A );

#ifndef WITHOUT_COMPLEX
template void elemental::advanced::internal::ApplyPackedReflectorsRUHF
( Conjugation conjugation, int offset,
  const DistMatrix<scomplex,MC,MR  >& H,
  const DistMatrix<scomplex,MD,STAR>& t,
        DistMatrix<scomplex,MC,MR  >& A );

template void elemental::advanced::internal::ApplyPackedReflectorsRUHF
( Conjugation conjugation, int offset,
  const DistMatrix<dcomplex,MC,MR  >& H,
  const DistMatrix<dcomplex,MD,STAR>& t,
        DistMatrix<dcomplex,MC,MR  >& A );
#endif

