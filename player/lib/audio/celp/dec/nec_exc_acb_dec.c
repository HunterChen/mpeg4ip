/*
This software module was originally developed by
Toshiyuki Nomura (NEC Corporation)
and edited by
Naoya Tanaka (Matsushita Communication Ind. Co., Ltd.)
in the course of development of the
MPEG-2 NBC/MPEG-4 Audio standard ISO/IEC 13818-7, 14496-1,2 and 3.
This software module is an implementation of a part of one or more
MPEG-2 NBC/MPEG-4 Audio tools as specified by the MPEG-2 NBC/MPEG-4 Audio
standard. ISO/IEC  gives users of the MPEG-2 NBC/MPEG-4 Audio standards
free license to this software module or modifications thereof for use in
hardware or software products claiming conformance to the MPEG-2 NBC/
MPEG-4 Audio  standards. Those intending to use this software module in
hardware or software products are advised that this use may infringe
existing patents. The original developer of this software module and
his/her company, the subsequent editors and their companies, and ISO/IEC
have no liability for use of this software module or modifications
thereof in an implementation. Copyright is not released for non
MPEG-2 NBC/MPEG-4 Audio conforming products. The original developer
retains full right to use the code for his/her  own purpose, assign or
donate the code to a third party and to inhibit third party from using
the code for non MPEG-2 NBC/MPEG-4 Audio conforming products.
This copyright notice must be included in all copies or derivative works.
Copyright (c)1996.
*/
/*
 *	MPEG-4 Audio Verification Model (LPC-ABS Core)
 *	
 *	Adaptive CB Decoding Subroutines
 *
 *	Ver1.0	96.12.16	T.Nomura(NEC)
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "buffersHandle.h"       /* handler, defines, enums */

#include "bitstream.h"
#include "lpc_common.h"
#include "nec_abs_const.h"
#include "nec_exc_proto.h"

#define NEC_PITCH_COEF	0.4

void nec_dec_acb(
		 float	ac[],		/* output */
		 long	ac_idx_opt,	/* input */
		 long	len_sf,		/* configuration input */
		 long	lagbit,		/* configuration input */
		 float	mem_past_exc[],	/* input */
		 long	*int_part,
		 long SampleRateMode)	/* output */
{
   long		i;
   float	*mem_ac, *zero;

   long pitch_max, pitch_iftap;

   /* Cofiguration Parameter Check */
   if(fs8kHz==SampleRateMode) {
     if ( lagbit != NEC_ACB_BIT ) {
        printf("\n Configuration error in nec_dec_acb \n");
        exit(1);
     }
     pitch_max = NEC_PITCH_MAX;
     pitch_iftap = NEC_PITCH_IFTAP;
   }else {
     if ( lagbit != NEC_ACB_BIT_WB ) {
        printf("\n Configuration error in nec_dec_acb \n");
        exit(1);
     }
     pitch_max = NEC_PITCH_MAX_FRQ16;
     pitch_iftap = NEC_PITCH_IFTAP16;
   }

   /*------ Memory Allocation ----------*/
   if ((zero =(float *)calloc(len_sf, sizeof(float))) == NULL) {
      printf("\n Memory allocation error in nec_dec_acb \n");
      exit(1);
   }
   if ((mem_ac =(float *)calloc(pitch_max+pitch_iftap+1+len_sf, sizeof(float))) == NULL) {
      printf("\n Memory allocation error in nec_dec_acb \n");
      exit(1);
   }

   for ( i = 0; i < pitch_max + pitch_iftap+1; i++)
      mem_ac[i] = mem_past_exc[i];
   for ( i = 0; i < len_sf; i++ ) zero[i] = 0.0;

   *int_part = nec_acb_generation(ac_idx_opt,len_sf,mem_ac,zero,ac,1.0,0, 
				  SampleRateMode);

  FREE( zero );
  FREE( mem_ac );
}

void nec_pitch_enhancement(
			   float exc[],		/* input */
			   float enh_exc[],	/* output */
			   float mem_pitch[],	/* input */
			   long  vu_flag,	/* input */
			   long  idx,		/* input */
			   long  len_sf,
			   long SampleRateMode )/* configuration input */
{
   long		i, I_part;
   float	*mem_ac;
   float	ac, cc, gain_pf, gain_norm;

   long idx_max, pitch_max, pitch_iftap;

   if(fs8kHz==SampleRateMode) {
     idx_max = 255;
     pitch_max = NEC_PITCH_MAX;
     pitch_iftap = NEC_PITCH_IFTAP;
   }else {
     idx_max = 511;
     pitch_max = NEC_PITCH_MAX_FRQ16;
     pitch_iftap = NEC_PITCH_IFTAP16;
   }

   if ((mem_ac =(float *)calloc(pitch_max+pitch_iftap+1+len_sf, sizeof(float))) == NULL) {
      printf("\n Memory allocation error in nec_pitch_enhancement \n");
      exit(1);
   }
   for (i = 0; i < pitch_max + pitch_iftap+1; i++)
      mem_ac[i] = mem_pitch[i];

   /*--- Pitch Enhancement ---*/
   if ( idx == idx_max || vu_flag == 0 ) {
      for (i = 0; i < len_sf; i++) enh_exc[i] = exc[i];
   } else {
      I_part = nec_acb_generation(idx,len_sf,mem_ac,exc,enh_exc,1.0,1, 
				  SampleRateMode);

      ac = cc = 0.0;
      for(i = 0; i < len_sf; i++){
	ac += enh_exc[i] * enh_exc[i];
	cc += exc[i] * enh_exc[i];
      }
      gain_pf = (ac != 0.0 ? cc/ac : 0.0);
      if ( gain_pf > 1.0 ) gain_pf = 1.0;
      if ( gain_pf < 0.0 ) gain_pf = 0.0;
      gain_pf = NEC_PITCH_COEF * gain_pf;
      gain_norm = 1.0/(1.0+gain_pf);

      for(i = 0; i < len_sf; i++){
	enh_exc[i] = gain_norm * (exc[i] + gain_pf * enh_exc[i]);
      }
   }

  FREE( mem_ac );
}
