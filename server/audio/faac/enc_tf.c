#include <math.h>
#include <stdlib.h>
#include <memory.h>
#include "aacenc.h"
#include "bitstream.h"
#include "interface.h"
#include "enc.h"
#include "block.h"
#include "tf_main.h"
#include "psych.h"
#include "mc_enc.h"
#include "ms.h"
#include "quant.h"
#include "all.h"
#include "aac_se_enc.h"
#include "nok_ltp_enc.h"
#include "transfo.h"


/* AAC tables */

/* First attempt at supporting multiple sampling rates   *
 * and bitrates correctly.                               */

/* Tables for maximum nomber of scalefactor bands */
/* Needs more fine-tuning. Only the values for 44.1kHz have been changed
   on lower bitrates. */
int max_sfb_s[] = { 12, 12, 12, 13, 14, 13, 15, 15, 15, 15, 15, 15 };
int max_sfb_l[] = { 49, 49, 47, 48, 49, 51, 47, 47, 43, 43, 43, 40 };


int     block_size_samples = 1024;  /* nr of samples per block in one! audio channel */
int     short_win_in_long  = 8;
int     max_ch;    /* no of of audio channels */
double *spectral_line_vector[MAX_TIME_CHANNELS];
double *reconstructed_spectrum[MAX_TIME_CHANNELS];
double *overlap_buffer[MAX_TIME_CHANNELS];
double *DTimeSigBuf[MAX_TIME_CHANNELS];
double *DTimeSigLookAheadBuf[MAX_TIME_CHANNELS*2];
double *nok_tmp_DTimeSigBuf[MAX_TIME_CHANNELS]; /* temporary fix to the buffer size problem. */

/* variables used by the T/F mapping */
enum QC_MOD_SELECT qc_select = AAC_QC;                   /* later f(encPara) */
enum AAC_PROFILE profile = MAIN;
enum WINDOW_TYPE block_type[MAX_TIME_CHANNELS];
enum WINDOW_TYPE desired_block_type[MAX_TIME_CHANNELS];
enum WINDOW_TYPE next_desired_block_type[MAX_TIME_CHANNELS];

/* Additional variables for AAC */
int aacAllowScalefacs = 1;              /* Allow AAC scalefactors to be nonconstant */
TNS_INFO tnsInfo[MAX_TIME_CHANNELS];
NOK_LT_PRED_STATUS nok_lt_status[MAX_TIME_CHANNELS];

AACQuantInfo quantInfo[MAX_TIME_CHANNELS];               /* Info structure for AAC quantization and coding */

/* Channel information */
Ch_Info channelInfo[MAX_TIME_CHANNELS];

/* AAC shorter windows 960-480-120 */
int useShortWindows=0;  /* don't use shorter windows */

// TEMPORARY HACK

int srate_idx;

int sampling_rate;
int bit_rate;

// END OF HACK


/* EncTfFree() */
/* Free memory allocated by t/f-based encoder core. */

void EncTfFree (void)
{
  int chanNum;

  for (chanNum=0;chanNum<MAX_TIME_CHANNELS;chanNum++) {
    if (DTimeSigBuf[chanNum]) free(DTimeSigBuf[chanNum]);
    if (spectral_line_vector[chanNum]) free(spectral_line_vector[chanNum]);

    if (reconstructed_spectrum[chanNum]) free(reconstructed_spectrum[chanNum]);
    if (overlap_buffer[chanNum]) free(overlap_buffer[chanNum]);
    if (nok_lt_status[chanNum].delay) free(nok_lt_status[chanNum].delay);
    if (nok_tmp_DTimeSigBuf[chanNum]) free(nok_tmp_DTimeSigBuf[chanNum]);
  }
  for (chanNum=0;chanNum<MAX_TIME_CHANNELS*2;chanNum++) {
    if (DTimeSigLookAheadBuf[chanNum]) free(DTimeSigLookAheadBuf[chanNum]);
  }
}

/*******************************************************************************
 ***
 *** Function: EncTfInit
 ***
 *** Purpose:  Initialize the T/F-part and the macro blocks of the T/F part of the VM
 ***
 *** Description:
 ***
 ***
 *** Parameters:
 ***
 ***
 *** Return Value:
 ***
 *** **** MPEG-4 VM ****
 ***
 ******************************************************************************/

void EncTfInit (faacAACStream *as)
{
  int chanNum, i;
  int SampleRates[] = { 96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,0};
//	int BitRates[] = {
//		64000,80000,96000,112000,128000,160000,192000,224000,256000,0
//	};

  sampling_rate = as->out_sampling_rate;
  bit_rate = as->bit_rate;

  for (i = 0; ; i++) {
    if (SampleRates[i] == sampling_rate) {
      srate_idx = i;
      break;
    }
  }

  profile = MAIN;
  qc_select = AAC_PRED;           /* enable prediction */

  if (as->profile == LOW) {
    profile = LOW;
    qc_select = AAC_QC;          /* disable prediction */
  }

  if (as->use_PNS)
    pns_sfb_start = 0;
  else
    pns_sfb_start = 60;

  /* set the return values */
  max_ch = as->channels;

  /* some global initializations */
  for (chanNum=0;chanNum<MAX_TIME_CHANNELS;chanNum++) {
    DTimeSigBuf[chanNum]            = (double*)malloc(block_size_samples*sizeof(double));
    memset(DTimeSigBuf[chanNum],0,(block_size_samples)*sizeof(double));
    spectral_line_vector[chanNum]   = (double*)malloc(2*block_size_samples*sizeof(double));
    reconstructed_spectrum[chanNum] = (double*)malloc(block_size_samples*sizeof(double));
    memset(reconstructed_spectrum[chanNum], 0, block_size_samples*sizeof(double));
    overlap_buffer[chanNum] = (double*)malloc(sizeof(double)*block_size_samples);
    memset(overlap_buffer[chanNum],0,(block_size_samples)*sizeof(double));
    block_type[chanNum] = ONLY_LONG_WINDOW;
    nok_lt_status[chanNum].delay =  (int*)malloc(MAX_SHORT_WINDOWS*sizeof(int));
    nok_tmp_DTimeSigBuf[chanNum]  = (double*)malloc(2*block_size_samples*sizeof(double));
    memset(nok_tmp_DTimeSigBuf[chanNum],0,(2*block_size_samples)*sizeof(double));
  }
  for (chanNum=0;chanNum<MAX_TIME_CHANNELS*2;chanNum++) {
    DTimeSigLookAheadBuf[chanNum]   = (double*)malloc((block_size_samples)*sizeof(double));
    memset(DTimeSigLookAheadBuf[chanNum],0,(block_size_samples)*sizeof(double));
  }

  /* initialize psychoacoustic module */
  EncTf_psycho_acoustic_init();

  /* initialize spectrum processing */
  /* initialize quantization and coding */
  tf_init_encode_spectrum_aac(0);

  /* Init TNS */
  for (chanNum=0;chanNum<MAX_TIME_CHANNELS;chanNum++) {
    TnsInit(sampling_rate,profile,&tnsInfo[chanNum]);
    quantInfo[chanNum].tnsInfo = &tnsInfo[chanNum];         /* Set pointer to TNS data */
  }

  /* Init LTP predictor */
  for (chanNum=0;chanNum<MAX_TIME_CHANNELS;chanNum++) {
    nok_init_lt_pred (&nok_lt_status[chanNum]);
    quantInfo[chanNum].ltpInfo = &nok_lt_status[chanNum];  /* Set pointer to LTP data */
    quantInfo[chanNum].prev_window_shape = WS_SIN;
  }

  for (chanNum=0;chanNum<MAX_TIME_CHANNELS;chanNum++) {
    quantInfo[chanNum].srate_idx = srate_idx;
    quantInfo[chanNum].profile = as->profile;
  }

  /* Initialisation for FFT & MDCT stuff */
  make_MDCT_windows();
  make_FFT_order();
  initrft();
}



/*******************************************************************************
 ***
 *** Function:    EncTfFrame
 ***
 *** Purpose:     processes a block of time signal input samples into a bitstream
 ***              based on T/F encoding
 ***
 *** Description:
 ***
 ***
 *** Parameters:
 ***
 ***
 *** Return Value:  returns the number of used bits
 ***
 *** **** MPEG-4 VM ****
 ***
 ******************************************************************************/

int EncTfFrame (faacAACStream *as, BsBitStream  *fixed_stream)
{
  int used_bits;
  int error;

  /* Energy array (computed before prediction for long windows) */
  double energy[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS];

  /* determine the function parameters used earlier */
  int          average_bits = as->frame_bits;
  int          available_bitreservoir_bits = as->available_bits-as->frame_bits;

  /* max. allowed amount of bits in the reservoir  (used to avoid padding bits) */
  long num_bits_available;

  double *p_ratio[MAX_TIME_CHANNELS], allowed_distortion[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS];
  static double p_ratio_long[2][MAX_TIME_CHANNELS][MAX_SCFAC_BANDS];
  static double p_ratio_short[2][MAX_TIME_CHANNELS][MAX_SCFAC_BANDS];
  int    nr_of_sfb[MAX_TIME_CHANNELS], sfb_width_table[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS];
  int sfb_offset_table[MAX_TIME_CHANNELS][MAX_SCFAC_BANDS+1];


  /* structures holding the output of the psychoacoustic model */
  CH_PSYCH_OUTPUT_LONG chpo_long[MAX_TIME_CHANNELS];
  CH_PSYCH_OUTPUT_SHORT chpo_short[MAX_TIME_CHANNELS][MAX_SHORT_WINDOWS];
  static int ps = 1;
  ps = !ps;

  if (as->header_type==ADTS_HEADER)
    available_bitreservoir_bits += 58;

  /***********************************************************************/
  /* Determine channel elements */
  /***********************************************************************/
  DetermineChInfo(channelInfo, max_ch);


  /***********************************************************************/
  /* Fill buffers  */
  /* store input data in look ahead buffer which may be necessary */
  /* for the window switching decision */
  /***********************************************************************/
  {
	  int chanNum;
	  int i;

	  for (chanNum = 0; chanNum < max_ch; chanNum++) {

		  if (channelInfo[chanNum].present) {
			  if ((channelInfo[chanNum].cpe) && (channelInfo[chanNum].ch_is_left)) { /* CPE */
				  int leftChan = chanNum;
				  int rightChan = channelInfo[chanNum].paired_ch;

				  if (as->use_MS == 1) {
					  for(i = 0; i < block_size_samples; i++){
						  DTimeSigLookAheadBuf[leftChan][i] = (as->inputBuffer[leftChan][i]+as->inputBuffer[rightChan][i])*0.5;
						  DTimeSigLookAheadBuf[rightChan][i] = (as->inputBuffer[leftChan][i]-as->inputBuffer[rightChan][i])*0.5;
					  }
				  }
			  }

			  if(as->use_LTP) {
				  for( i=0; i<block_size_samples; i++ ) {
					  nok_tmp_DTimeSigBuf[chanNum][i] = DTimeSigBuf[chanNum][i];
					  nok_tmp_DTimeSigBuf[chanNum][block_size_samples + i] = DTimeSigLookAheadBuf[chanNum][i];
				  }
			  }
			  for( i=0; i<block_size_samples; i++ ) {
				  DTimeSigBuf[chanNum][i] = DTimeSigLookAheadBuf[chanNum][i];
				  DTimeSigLookAheadBuf[chanNum][i] = as->inputBuffer[chanNum][i];
			  }
		  }
	  }
  }


  if (fixed_stream == NULL) {
    psy_fill_lookahead(DTimeSigLookAheadBuf, max_ch);
    return FNO_ERROR; /* quick'n'dirty fix for encoder startup */
  }

  /* Keep track of number of bits used */
  used_bits = 0;

  /*****************************************************************************/
  /* psychoacoustics */
  /*****************************************************************************/
  EncTf_psycho_acoustic(
	  sampling_rate,
	  max_ch,
	  channelInfo,
	  DTimeSigLookAheadBuf,
	  next_desired_block_type,
	  as->use_MS,
	  chpo_long,
	  chpo_short
	  );

  /*****************************************************************************/
  /* block_switch processing */
  /*****************************************************************************/
  {
    int chanNum;
	
    for (chanNum=0;chanNum<max_ch;chanNum++) {
    /* A few definitions:                                                      */
    /*   block_type:  Initially, the block_type used in the previous frame.    */
    /*                Will be set to the block_type to use this frame.         */
    /*                A block type will be selected to ensure a meaningful     */
    /*                window transition.                                       */
    /*   next_desired_block_type:  Block_type (LONG or SHORT) which the psycho */
    /*                model wants to use next frame.  The psycho model is      */
    /*                using a look-ahead buffer.                               */
    /*   desired_block_type:  Block_type (LONG or SHORT) which the psycho      */
    /*                previously wanted to use.  It is the desired block_type  */
    /*                for this frame.                                          */
    if ( (block_type[chanNum]==ONLY_SHORT_WINDOW)||(block_type[chanNum]==LONG_SHORT_WINDOW) ) {
      if ( (desired_block_type[chanNum]==ONLY_LONG_WINDOW)&&(next_desired_block_type[chanNum]==ONLY_LONG_WINDOW) ) {
	block_type[chanNum]=SHORT_LONG_WINDOW;
      }
      else {
	block_type[chanNum]=ONLY_SHORT_WINDOW;
      }
    }
    else if (next_desired_block_type[chanNum]==ONLY_SHORT_WINDOW) {
      block_type[chanNum]=LONG_SHORT_WINDOW;
      }
      else {
	block_type[chanNum]=ONLY_LONG_WINDOW;
      }
      desired_block_type[chanNum]=next_desired_block_type[chanNum];
    }
  }

//	printf("%d\t\n", block_type[0]);
//	block_type[0] = ONLY_LONG_WINDOW;
//	block_type[1] = ONLY_LONG_WINDOW;
//	block_type[0] = ONLY_SHORT_WINDOW;
//	block_type[1] = ONLY_SHORT_WINDOW;

  {
    int chanNum;

    for (chanNum=0;chanNum<max_ch;chanNum++) {
      /* Set window shape paremeter in quantInfo */
      quantInfo[chanNum].prev_window_shape = quantInfo[chanNum].window_shape;
      if (block_type[chanNum] == ONLY_SHORT_WINDOW)
		  quantInfo[chanNum].window_shape = WS_KBD;
	  else
		  quantInfo[chanNum].window_shape = WS_SIN;

      switch( block_type[chanNum] ) {
        case ONLY_SHORT_WINDOW  :
//      no_sub_win   = short_win_in_long;
//      sub_win_size = block_size_samples/short_win_in_long;
        quantInfo[chanNum].max_sfb = max_sfb_s[srate_idx];
#if 0
        quantInfo[chanNum].num_window_groups = 4;
        quantInfo[chanNum].window_group_length[0] = 1;
        quantInfo[chanNum].window_group_length[1] = 2;
        quantInfo[chanNum].window_group_length[2] = 3;
        quantInfo[chanNum].window_group_length[3] = 2;
#else
        quantInfo[chanNum].num_window_groups = 1;
        quantInfo[chanNum].window_group_length[0] = 8;
        quantInfo[chanNum].window_group_length[1] = 0;
        quantInfo[chanNum].window_group_length[2] = 0;
        quantInfo[chanNum].window_group_length[3] = 0;
        quantInfo[chanNum].window_group_length[4] = 0;
        quantInfo[chanNum].window_group_length[5] = 0;
        quantInfo[chanNum].window_group_length[6] = 0;
        quantInfo[chanNum].window_group_length[7] = 0;
#endif
        break;

        default:
//      no_sub_win   = 1;
//      sub_win_size = block_size_samples;
        quantInfo[chanNum].max_sfb = max_sfb_l[srate_idx];
        quantInfo[chanNum].num_window_groups = 1;
        quantInfo[chanNum].window_group_length[0]=1;
        break;
      }
    }
  }

  {
    int chanNum;
    for (chanNum=0;chanNum<max_ch;chanNum++) {
      /* Count number of bits used for gain_control_data */
      used_bits += WriteGainControlData(NULL,0); /* Zero write flag means don't write */
    }
  }


  /*****************************************************************************
  * T/F mapping (MDCT)
  *****************************************************************************/
  {
    int chanNum, k;
    for (chanNum=0;chanNum<max_ch;chanNum++) {
      buffer2freq(
		  DTimeSigBuf[chanNum],
		  spectral_line_vector[chanNum],
		  overlap_buffer[chanNum],
		  block_type[chanNum],
		  quantInfo[chanNum].window_shape,
		  quantInfo[chanNum].prev_window_shape,
		  MOVERLAPPED
		  );

      if (block_type[chanNum] == ONLY_SHORT_WINDOW) {
	for (k = 0; k < 8; k++) {
	  specFilter(spectral_line_vector[chanNum]+k*BLOCK_LEN_SHORT, spectral_line_vector[chanNum]+k*BLOCK_LEN_SHORT, as->out_sampling_rate, as->cut_off, BLOCK_LEN_SHORT);
        }
      }
      else {
	specFilter(spectral_line_vector[chanNum], spectral_line_vector[chanNum], as->out_sampling_rate, as->cut_off, BLOCK_LEN_LONG);
      }
    }
  }

  /*****************************************************************************
  * adapt ratios of psychoacoustic module to codec scale factor bands
  *****************************************************************************/

  {
    int chanNum;
    for (chanNum=0;chanNum<max_ch;chanNum++) {
      switch( block_type[chanNum] ) {
	case ONLY_LONG_WINDOW:
	  memcpy( (char*)sfb_width_table[chanNum], (char*)chpo_long[chanNum].cb_width, (NSFB_LONG+1)*sizeof(int) );
	  nr_of_sfb[chanNum] = chpo_long[chanNum].no_of_cb;
	  p_ratio[chanNum]   = p_ratio_long[ps][chanNum];
	  break;
        case LONG_SHORT_WINDOW:
	  memcpy( (char*)sfb_width_table[chanNum], (char*)chpo_long[chanNum].cb_width, (NSFB_LONG+1)*sizeof(int) );
	  nr_of_sfb[chanNum] = chpo_long[chanNum].no_of_cb;
	  p_ratio[chanNum]   = p_ratio_long[ps][chanNum];
	  break;
        case ONLY_SHORT_WINDOW:
	  memcpy( (char*)sfb_width_table[chanNum], (char*)chpo_short[chanNum][0].cb_width, (NSFB_SHORT+1)*sizeof(int) );
	  nr_of_sfb[chanNum] = chpo_short[chanNum][0].no_of_cb;
	  p_ratio[chanNum]   = p_ratio_short[ps][chanNum];
          break;
        case SHORT_LONG_WINDOW:
	  memcpy( (char*)sfb_width_table[chanNum], (char*)chpo_long[chanNum].cb_width, (NSFB_LONG+1)*sizeof(int) );
	  nr_of_sfb[chanNum] = chpo_long[chanNum].no_of_cb;
	  p_ratio[chanNum]   = p_ratio_long[ps][chanNum];
          break;
      }
    }
  }

  MSPreprocess(p_ratio_long[!ps], p_ratio_short[!ps], chpo_long, chpo_short,
		channelInfo, block_type, quantInfo, as->use_MS, max_ch);

  MSEnergy(spectral_line_vector, energy, chpo_long, chpo_short, sfb_width_table,
		channelInfo, block_type, quantInfo, as->use_MS, max_ch);

  {
    int chanNum;
    for (chanNum=0;chanNum<max_ch;chanNum++) {
      /* Construct sf band offset table */
      int offset=0;
      int sfb;
      for (sfb=0;sfb<nr_of_sfb[chanNum];sfb++) {
	sfb_offset_table[chanNum][sfb] = offset;
	offset+=sfb_width_table[chanNum][sfb];
      }
      sfb_offset_table[chanNum][nr_of_sfb[chanNum]]=offset;
    }
  }

  /*****************************************************************************
  * quantization and coding
  *****************************************************************************/
  {
//  int padding_limit = max_bitreservoir_bits;
    int maxNumBitsByteAligned;
    int chanNum;
    int numFillBits;
    int bitsLeftAfterFill;
    int orig_used_bits;

    /* bit budget */
    num_bits_available = (long)(average_bits + available_bitreservoir_bits - used_bits);

    /* find the largest byte-aligned section with fewer bits than num_bits_available */
    maxNumBitsByteAligned = ((num_bits_available >> 3) << 3);

    /* Compute how many reservoir bits can be used and still be able to byte */
    /* align without exceeding num_bits_available, and have room for an ID_END marker   */
    available_bitreservoir_bits = maxNumBitsByteAligned - LEN_SE_ID - average_bits;

    /******************************************/
    /* Perform TNS analysis and filtering     */
    /******************************************/
    for (chanNum=0;chanNum<max_ch;chanNum++) {
      error = TnsEncode(nr_of_sfb[chanNum],            /* Number of bands per window */
      		        quantInfo[chanNum].max_sfb,              /* max_sfb */
          	        block_type[chanNum],
	  	        sfb_offset_table[chanNum],
		        spectral_line_vector[chanNum],
		        &tnsInfo[chanNum],
		        as->use_TNS);
      if (error == FERROR)
      return FERROR;
    }

    /*******************************************************************************/
    /* If LTP prediction is used, compute LTP predictor info and residual spectrum */
    /*******************************************************************************/
    for(chanNum=0;chanNum<max_ch;chanNum++) {
      if(as->use_LTP && (block_type[chanNum] != ONLY_SHORT_WINDOW)) {
        if(channelInfo[chanNum].cpe) {
    	  if(channelInfo[chanNum].ch_is_left) {
	    int i;
	    int leftChan=chanNum;
	    int rightChan=channelInfo[chanNum].paired_ch;

  	    nok_ltp_enc(spectral_line_vector[leftChan],
		        nok_tmp_DTimeSigBuf[leftChan],
		        block_type[leftChan],
		        WS_SIN,
		        &sfb_offset_table[leftChan][0],
		        nr_of_sfb[leftChan],
		        &nok_lt_status[leftChan]);

            nok_lt_status[rightChan].global_pred_flag = nok_lt_status[leftChan].global_pred_flag;
  	    for(i = 0; i < BLOCK_LEN_LONG; i++)
    	      nok_lt_status[rightChan].pred_mdct[i] = nok_lt_status[leftChan].pred_mdct[i];
  	    for(i = 0; i < MAX_SCFAC_BANDS; i++)
  	      nok_lt_status[rightChan].sfb_prediction_used[i] = nok_lt_status[leftChan].sfb_prediction_used[i];
  	    nok_lt_status[rightChan].weight = nok_lt_status[leftChan].weight;
	    nok_lt_status[rightChan].delay[0] = nok_lt_status[leftChan].delay[0];

	    if (!channelInfo[leftChan].common_window) {
	      nok_ltp_enc(spectral_line_vector[rightChan],
			  nok_tmp_DTimeSigBuf[rightChan],
			  block_type[rightChan],
			  WS_SIN,
			  &sfb_offset_table[rightChan][0],
			  nr_of_sfb[rightChan],
			  &nok_lt_status[rightChan]);
            }
          } /* if(channelInfo[chanNum].ch_is_left) */
        } /* if(channelInfo[chanNum].cpe) */
        else
	  nok_ltp_enc(spectral_line_vector[chanNum],
		      nok_tmp_DTimeSigBuf[chanNum],
		      block_type[chanNum],
		      WS_SIN,
		      &sfb_offset_table[chanNum][0],
		      nr_of_sfb[chanNum],
		      &nok_lt_status[chanNum]);
      } /* if(channelInfo[chanNum].present... */
      else
        quantInfo[chanNum].ltpInfo->global_pred_flag = 0;
    } /* for(chanNum... */

    /******************************************/
    /* Apply MS stereo                        */
    /******************************************/
    if (as->use_MS == 1) {
      MSEncode(spectral_line_vector,
	       channelInfo,
	       sfb_offset_table,
	       block_type,
	       quantInfo,
	       max_ch);
    }
    else if (as->use_MS == 0) {
      MSEncodeSwitch(spectral_line_vector,
		     channelInfo,
		     sfb_offset_table,
//		     block_type,
		     quantInfo,
		     max_ch
                     );
    }

    /************************************************/
    /* Call the AAC quantization and coding module. */
    /************************************************/
    for (chanNum = 0; chanNum < max_ch; chanNum++) {
      int bitsToUse;
      bitsToUse = (int)((average_bits - used_bits)/max_ch);
      bitsToUse += (int)(0.2*available_bitreservoir_bits/max_ch);

      error = tf_encode_spectrum_aac(&spectral_line_vector[chanNum],
                                     &p_ratio[chanNum],
                                     &allowed_distortion[chanNum],
                                     &energy[chanNum],
                                     &block_type[chanNum],
                                     &sfb_width_table[chanNum],
//				     &nr_of_sfb[chanNum],
                                     bitsToUse,
//				     available_bitreservoir_bits,
//				     padding_limit,
				     fixed_stream,
//				     NULL,
//				     1,             /* nr of audio channels */
				     &reconstructed_spectrum[chanNum],
//				     useShortWindows,
//				     aacAllowScalefacs,
				     &quantInfo[chanNum],
				     &(channelInfo[chanNum])
//				     ,0/*no vbr*/,
//				     ,bit_rate
                                     );
      if (error == FERROR)
        return error;
    }

    /**********************************************************/
    /* Reconstruct MS Stereo bands for prediction            */
    /**********************************************************/
    if (as->use_MS != -1) {
      MSReconstruct(reconstructed_spectrum,
		    channelInfo,
		    sfb_offset_table,
//		    block_type,
		    quantInfo,
		    max_ch);
    }

    /**********************************************************/
    /* Update LTP history buffer                              */
    /**********************************************************/
    if(as->use_LTP)
      for (chanNum=0;chanNum<max_ch;chanNum++) {
        nok_ltp_reconstruct(reconstructed_spectrum[chanNum],
			    block_type[chanNum],
			    WS_SIN,
			    &sfb_offset_table[chanNum][0],
			    nr_of_sfb[chanNum],
			    &nok_lt_status[chanNum]);
      }


    /**********************************/
    /* Write out all encoded channels */
    /**********************************/
    used_bits = 0;
    if (as->header_type==ADTS_HEADER)
      used_bits += WriteADTSHeader(&quantInfo[0], fixed_stream, used_bits, 0);

    for (chanNum=0;chanNum<max_ch;chanNum++) {
      if (channelInfo[chanNum].present) {
        /* Write out a single_channel_element */
        if (!channelInfo[chanNum].cpe) {
	  /* Write out sce */ /* BugFix by YT  '+=' sould be '=' */
          used_bits += WriteSCE(&quantInfo[chanNum],   /* Quantization information */
			        channelInfo[chanNum].tag,
			        fixed_stream,           /* Bitstream */
			        0);                     /* Write flag, 1 means write */
        }
        else {
	  if (channelInfo[chanNum].ch_is_left) {
	    /* Write out cpe */
	    used_bits += WriteCPE(&quantInfo[chanNum],   /* Quantization information,left */
				  &quantInfo[channelInfo[chanNum].paired_ch],   /* Right */
				  channelInfo[chanNum].tag,
				  channelInfo[chanNum].common_window,    /* common window */
				  &(channelInfo[chanNum].ms_info),
				  fixed_stream,           /* Bitstream */
				  0);                     /* Write flag, 1 means write */
          }
        }  /* if (!channelInfo[chanNum].cpe)  else */
      } /* if (chann...*/
    } /* for (chanNum...*/

    orig_used_bits = used_bits;

    /* Compute how many fill bits are needed to avoid overflowing bit reservoir */
    /* Save room for ID_END terminator */
    if (used_bits < (8 - LEN_SE_ID) ) {
      numFillBits = 8 - LEN_SE_ID - used_bits;
    }
    else {
      numFillBits = 0;
    }

    /* Write AAC fill_elements, smallest fill element is 7 bits. */
    /* Function may leave up to 6 bits left after fill, so tell it to fill a few extra */
    numFillBits += 6;
    bitsLeftAfterFill=WriteAACFillBits(fixed_stream,numFillBits, 0);
    used_bits += (numFillBits - bitsLeftAfterFill);

    /* Write ID_END terminator */
    used_bits += LEN_SE_ID;

    /* Now byte align the bitstream */
    used_bits += ByteAlign(fixed_stream, 0);

    if (as->header_type==ADTS_HEADER)
      WriteADTSHeader(&quantInfo[0], fixed_stream, used_bits, 1);

    for (chanNum=0;chanNum<max_ch;chanNum++) {
      if (channelInfo[chanNum].present) {
        /* Write out a single_channel_element */
        if (!channelInfo[chanNum].cpe) {
          /* Write out sce */
	  WriteSCE(&quantInfo[chanNum],   /* Quantization information */
		   channelInfo[chanNum].tag,
		   fixed_stream,           /* Bitstream */
		   1);                     /* Write flag, 1 means write */
        }
        else {
       	  if (channelInfo[chanNum].ch_is_left) {
	    /* Write out cpe */
	    WriteCPE(&quantInfo[chanNum],   /* Quantization information,left */
		     &quantInfo[channelInfo[chanNum].paired_ch],   /* Right */
		     channelInfo[chanNum].tag,
		     channelInfo[chanNum].common_window,    /* common window */
		     &(channelInfo[chanNum].ms_info),
		     fixed_stream,           /* Bitstream */
		     1);                     /* Write flag, 1 means write */
          }
        }  /* if (!channelInfo[chanNum].cpe)  else */
      } /* if (chann...*/
    } /* for (chanNum...*/

    /* Compute how many fill bits are needed to avoid overflowing bit reservoir */
    /* Save room for ID_END terminator */
    if (orig_used_bits < (8 - LEN_SE_ID) ) {
    numFillBits = 8 - LEN_SE_ID - used_bits;
    }
    else {
      numFillBits = 0;
    }

    /* Write AAC fill_elements, smallest fill element is 7 bits. */
    /* Function may leave up to 6 bits left after fill, so tell it to fill a few extra */
    numFillBits += 6;
    bitsLeftAfterFill=WriteAACFillBits(fixed_stream,numFillBits, 1);

    /* Write ID_END terminator */
    BsPutBit(fixed_stream,ID_END,LEN_SE_ID);

    /* Now byte align the bitstream */
    ByteAlign(fixed_stream, 1);

  } /* End of quantization and coding */
  return FNO_ERROR;
}

