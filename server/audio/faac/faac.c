/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * $Id: faac.c,v 1.4 2001/06/01 22:30:45 wmaycisco Exp $
 */

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __unix__
#define min(a,b) ( (a) < (b) ? (a) : (b) )
#include <sys/time.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <sndfile.h>

#include "faac.h"


int main(int argc, char *argv[])
{
	int i, frames, currentFrame;
	faacEncHandle hEncoder;
	SNDFILE *infile;
	SF_INFO sfinfo;
	faacEncConfiguration myFormat;

	unsigned long totalDesiredBitRate = 0;
	int rawInput = 0;
	unsigned long rawSampleRate = 44100;
	int swapRawBytes = 0;

	unsigned long samplesInput, maxBytesOutput;

	short *pcmbuf;

	unsigned char *bitbuf;
	int bytesInput = 0;

	FILE *outfile;

#ifdef __unix__
	struct rusage usage;
#endif
#ifdef _WIN32
	long begin, end;
	int nTotSecs, nSecs;
	int nMins;
#else
	float totalSecs;
	int mins;
#endif

	/* get the default encoder configuration values */
	hEncoder = faacEncOpen(44100, 2, &samplesInput, &maxBytesOutput);
	memcpy(&myFormat, faacEncGetCurrentConfiguration(hEncoder),
		sizeof(myFormat));
	faacEncClose(hEncoder);
	hEncoder = 0;

	printf("FAAC - command line demo of %s\n", __DATE__);
	printf("Uses FAACLIB version: %.1f %s\n\n", FAACENC_VERSION, (FAACENC_VERSIONB)?"beta":"");

	if (argc < 3)
	{
		printf("USAGE: %s -options infile outfile\n", argv[0]);
		printf("Options:\n");
		printf("  -mX   AAC MPEG version, X can be 2 or 4.\n");
		printf("  -pX   AAC object type, X can be LC, MAIN or LTP.\n");
		printf("  -nm   Don\'t use mid/side coding.\n");
		printf("  -tns  Use TNS coding.\n");
		printf("  -bX   Set the total bitrate, X in Kbps.\n\n");
		printf("  -brX  Set the bitrate per channel, X in bps.\n\n");
		printf("  -bwX  Set the bandwidth, X in Hz.\n");
		printf("  -rX   Input is raw 16 bit stereo PCM at X Hz, default 44100.\n");
		printf("  -x    Swap raw input bytes.\n");
		return 1;
	}

	/* set other options */
	if (argc > 3)
	{
		for (i = 1; i < argc-2; i++) {
			if ((argv[i][0] == '-') || (argv[i][0] == '/')) {
				switch(argv[i][1]) {
				case 'p': case 'P':
					if ((argv[i][2] == 'l') || (argv[i][2] == 'L')) {
						if ((argv[i][3] == 'c') || (argv[i][2] == 'C')) {
							myFormat.aacObjectType = LOW;
						} else if ((argv[i][3] == 't') || (argv[i][2] == 'T')) {
							myFormat.aacObjectType = LTP;
						}
					} else if ((argv[i][2] == 'm') || (argv[i][2] == 'M')) {
						myFormat.aacObjectType = MAIN;
					}
				break;
				case 'm': case 'M':
					if (argv[i][2] == '4') {
						myFormat.mpegVersion = MPEG4;
					} else {
						myFormat.mpegVersion = MPEG2;
					}
				break;
				case 't': case 'T':
					if ((argv[i][2] == 'n') || (argv[i][2] == 'N'))
						myFormat.useTns = 1;
				break;
				case 'n': case 'N':
					if ((argv[i][2] == 'm') || (argv[i][2] == 'M'))
						myFormat.allowMidside = 0;
				break;
				case 'b': case 'B':
					if ((argv[i][2] == 'r') || (argv[i][2] == 'R'))
					{
						unsigned int bitrate = atol(&argv[i][3]);
						if (bitrate)
						{
							myFormat.bitRate = bitrate;
						}
					} else if ((argv[i][2] == 'w') || (argv[i][2] == 'W')) {
						unsigned int bandwidth = atol(&argv[i][3]);
						if (bandwidth)
						{
							myFormat.bandWidth = bandwidth;
						}
					} else if (isdigit(argv[i][2])) {
						unsigned int bitrate = atol(&argv[i][2]);
						if (bitrate)
						{
							totalDesiredBitRate = bitrate * 1000;
						}
					}
				break;
				case 'r':
					rawInput = 1;
					if (isdigit(argv[i][2])) {
						rawSampleRate = atol(&argv[i][3]);
					} else {
						rawSampleRate = 44100;
					}
				break;
				case 'x':
					swapRawBytes = 1;
				break;
				}
			}
		}
	}

	/* handle raw audio input */
    if (rawInput) {
		sfinfo.format =  SF_FORMAT_RAW;
		if (swapRawBytes) {
			if (IsBigEndian()) {
				sfinfo.format |= SF_FORMAT_PCM_LE;
			} else {
				sfinfo.format |= SF_FORMAT_PCM_BE;
			}
		} else {
			if (IsBigEndian()) {
				sfinfo.format |= SF_FORMAT_PCM_BE;
			} else {
				sfinfo.format |= SF_FORMAT_PCM_LE;
			}
		}
		sfinfo.pcmbitwidth = 16;
		sfinfo.channels = 2;
		sfinfo.samplerate = rawSampleRate;
    }

	/* open the audio input file */
	infile = sf_open_read(argv[argc-2], &sfinfo);
	if (infile == NULL)
	{
		printf("couldn't open input file %s\n", argv [argc-2]);
		return 1;
	}

	/* open the aac output file */
	outfile = fopen(argv[argc-1], "wb");
	if (!outfile)
	{
		printf("couldn't create output file %s\n", argv [argc-1]);
		return 1;
	}

	/* open the encoder library */
	hEncoder = faacEncOpen(sfinfo.samplerate, sfinfo.channels, 
		&samplesInput, &maxBytesOutput);

	if (totalDesiredBitRate) {
		myFormat.bitRate = totalDesiredBitRate / sfinfo.channels;
	} 

	if (!faacEncSetConfiguration(hEncoder, &myFormat))
		fprintf(stderr, "unsupported output format!\n");

	pcmbuf = (short*)malloc(samplesInput*sizeof(short));
	bitbuf = (unsigned char*)malloc(maxBytesOutput*sizeof(unsigned char));

	if (outfile)
	{
#ifdef _WIN32
		begin = GetTickCount();
#endif
		frames = (int)(sfinfo.samples/1024+0.5);
		currentFrame = 0;

		/* encoding loop */
		for ( ;; )
		{
			int bytesWritten;

			currentFrame++;

			bytesInput = sf_read_short(infile, pcmbuf, samplesInput) * sizeof(short);

			/* call the actual encoding routine */
			bytesWritten = faacEncEncode(hEncoder,
				pcmbuf,
				bytesInput/2,
				bitbuf,
				maxBytesOutput);

#ifndef _DEBUG
			printf("%.2f%%\tBusy encoding %s.\r",
				min((double)(currentFrame*100)/frames,100), argv[argc-2]);
#endif

			/* all done, bail out */
			if (!bytesInput && !bytesWritten)
				break ;

			if (bytesWritten < 0)
			{
				fprintf(stderr, "faacEncEncode() failed\n");
				break ;
			}

			/* write bitstream to aac file */
			fwrite(bitbuf, 1, bytesWritten, outfile);
		}

		/* clean up */
		fclose(outfile);

#ifdef _WIN32
		end = GetTickCount();
		nTotSecs = (end-begin)/1000;
		nMins = nTotSecs / 60;
		nSecs = nTotSecs - (60*nMins);
		printf("Encoding %s took:\t%d:%.2d\t\n", argv[argc-2], nMins, nSecs);
#else
#ifdef __unix__
		if (getrusage(RUSAGE_SELF, &usage) == 0) {
			totalSecs = usage.ru_utime.tv_sec;
			mins = totalSecs/60;
			printf("Encoding %s took: %i min, %.2f sec. of cpu-time\n", argv[argc-2], mins, totalSecs - (60 * mins));
		}
#else
		totalSecs = (float)(clock())/(float)CLOCKS_PER_SEC;
		mins = totalSecs/60;
		printf("Encoding %s took: %i min, %.2f sec.\n", argv[argc-2], mins, totalSecs - (60 * mins));
#endif
#endif
	}

	faacEncClose(hEncoder);

	sf_close(infile);

	if (pcmbuf) free(pcmbuf);
	if (bitbuf) free(bitbuf);

	return 0;
}
