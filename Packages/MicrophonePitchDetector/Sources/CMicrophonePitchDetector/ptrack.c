/*
 * PTrack
 *
 * This code has been extracted from the Csound opcode "ptrack".
 * It has been modified to work as a Soundpipe module and modified again for use in ZenTuner.
 *
 * Original Author(s): Victor Lazzarini, Miller Puckette (Original Algorithm), Aurelius Prochazka
 * Year: 2007
 * Location: Opcodes/pitchtrack.c
 *
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "CMicrophonePitchDetector.h"

#define MINFREQINBINS 5
#define MINBW 0.03
#define BPEROOVERLOG2 69.24936196
#define FACTORTOBINS 4/0.0145453
#define BINGUARD 10
#define PARTIALDEVIANCE 0.023
#define DBSCAL 3.333
#define MINBIN 3

#define THRSH 10.

#define COEF1 ((float)(.5 * 1.227054))
#define COEF2 ((float)(.5 * -0.302385))
#define COEF3 ((float)(.5 * 0.095326))
#define COEF4 ((float)(.5 * -0.022748))
#define COEF5 ((float)(.5 * 0.002533))
#define FLTLEN 5

void ptrack_set_spec(zt_ptrack *p)
{
    float *spec = (float *)p->spec1.ptr;
    float *spectmp = (float *)p->spec2.ptr;
    float *sig = (float *)p->signal.ptr;
    float *sinus  = (float *)p->sin.ptr;
    float *prev  = (float *)p->prev.ptr;
    int i, j, k, hop = p->hopsize, n = 2*hop;
    int halfhop = hop>>1;

    for (i = 0, k = 0; i < hop; i++, k += 2) {
        spec[k]   = sig[i] * sinus[k];
        spec[k+1] = sig[i] * sinus[k+1];
    }

    zt_fft_cpx(&p->fft, spec, hop);

    for (i = 0, k = 2*FLTLEN; i < hop; i+=2, k += 4) {
        spectmp[k]   = spec[i];
        spectmp[k+1] = spec[i+1];
    }

    for (i = n - 2, k = 2*FLTLEN+2; i >= 0; i-=2, k += 4) {
        spectmp[k]   = spec[i];
        spectmp[k+1] = -spec[i+1];
    }

    for (i = (2*FLTLEN), k = (2*FLTLEN-2);i<FLTLEN*4; i+=2, k-=2) {
        spectmp[k]   = spectmp[i];
        spectmp[k+1] = -spectmp[i+1];
    }

    for (i = (2*FLTLEN+n-2), k =(2*FLTLEN+n); i>=0; i-=2, k+=2) {
        spectmp[k]   = spectmp[i];
        spectmp[k+1] = -spectmp[k+1];
    }

    for (i = j = 0, k = 2*FLTLEN; i < halfhop; i++, j+=8, k+=2) {
        float re,  im;

        re= COEF1 * ( prev[k-2] - prev[k+1]  + spectmp[k-2] - prev[k+1]) +
            COEF2 * ( prev[k-3] - prev[k+2]  + spectmp[k-3]  - spectmp[ 2]) +
            COEF3 * (-prev[k-6] +prev[k+5]  -spectmp[k-6] +spectmp[k+5]) +
            COEF4 * (-prev[k-7] +prev[k+6]  -spectmp[k-7] +spectmp[k+6]) +
            COEF5 * ( prev[k-10] -prev[k+9]  +spectmp[k-10] -spectmp[k+9]);

        im= COEF1 * ( prev[k-1] +prev[k]  +spectmp[k-1] +spectmp[k]) +
            COEF2 * (-prev[k-4] -prev[k+3]  -spectmp[k-4] -spectmp[k+3]) +
            COEF3 * (-prev[k-5] -prev[k+4]  -spectmp[k-5] -spectmp[k+4]) +
            COEF4 * ( prev[k-8] +prev[k+7]  +spectmp[k-8] +spectmp[k+7]) +
            COEF5 * ( prev[k-9] +prev[k+8]  +spectmp[k-9] +spectmp[k+8]);

        spec[j]   = 0.707106781186547524400844362104849 * (re + im);
        spec[j+1] = 0.707106781186547524400844362104849 * (im - re);
        spec[j+4] = prev[k] + spectmp[k+1];
        spec[j+5] = prev[k+1] - spectmp[k];

        j += 8;
        k += 2;

        re= COEF1 * ( prev[k-2] -prev[k+1]  -spectmp[k-2] +spectmp[k+1]) +
            COEF2 * ( prev[k-3] -prev[k+2]  -spectmp[k-3] +spectmp[k+2]) +
            COEF3 * (-prev[k-6] +prev[k+5]  +spectmp[k-6] -spectmp[k+5]) +
            COEF4 * (-prev[k-7] +prev[k+6]  +spectmp[k-7] -spectmp[k+6]) +
            COEF5 * ( prev[k-10] -prev[k+9]  -spectmp[k-10] +spectmp[k+9]);

        im= COEF1 * ( prev[k-1] +prev[k]  -spectmp[k-1] -spectmp[k]) +
            COEF2 * (-prev[k-4] -prev[k+3]  +spectmp[k-4] +spectmp[k+3]) +
            COEF3 * (-prev[k-5] -prev[k+4]  +spectmp[k-5] +spectmp[k+4]) +
            COEF4 * ( prev[k-8] +prev[k+7]  -spectmp[k-8] -spectmp[k+7]) +
            COEF5 * ( prev[k-9] +prev[k+8]  -spectmp[k-9] -spectmp[k+8]);

        spec[j]   = 0.707106781186547524400844362104849 * (re + im);
        spec[j+1] = 0.707106781186547524400844362104849 * (im - re);
        spec[j+4] = prev[k] - spectmp[k+1];
        spec[j+5] = prev[k+1] + spectmp[k];

    }


    for (i = 0; i < n + 4*FLTLEN; i++) prev[i] = spectmp[i];

    for (i = 0; i < MINBIN; i++) spec[4*i + 2] = spec[4*i + 3] =0.0;
}

void ptrack_pt2(int *npeak, int numpks, PEAK *peaklist, float totalpower, float *spec, int n)
{
    int i;

    for (i = 4*MINBIN;i < (4*(n-2)) && *npeak < numpks; i+=4) {
        float height = spec[i+2], h1 = spec[i-2], h2 = spec[i+6];
        float totalfreq, peakfr, tmpfr1, tmpfr2, m, var, stdev;

        if (height < h1 || height < h2 ||
        h1 < 0.00001*totalpower ||
        h2 < 0.00001*totalpower) continue;

        peakfr= ((spec[i-8] - spec[i+8]) * (2.0 * spec[i] -
                                    spec[i+8] - spec[i-8]) +
         (spec[i-7] - spec[i+9]) * (2.0 * spec[i+1] -
                                    spec[i+9] - spec[i-7])) /
        (height + height);
        tmpfr1=  ((spec[i-12] - spec[i+4]) *
          (2.0 * spec[i-4] - spec[i+4] - spec[i-12]) +
          (spec[i-11] - spec[i+5]) * (2.0 * spec[i-3] -
                                      spec[i+5] - spec[i-11])) /
        (2.0 * h1) - 1;
        tmpfr2= ((spec[i-4] - spec[i+12]) * (2.0 * spec[i+4] -
                                     spec[i+12] - spec[i-4]) +
         (spec[i-3] - spec[i+13]) * (2.0 * spec[i+5] -
                                     spec[i+13] - spec[i-3])) /
        (2.0 * h2) + 1;


        m = 0.333333333333 * (peakfr + tmpfr1 + tmpfr2);
        var = 0.5 * ((peakfr-m)*(peakfr-m) +
                 (tmpfr1-m)*(tmpfr1-m) + (tmpfr2-m)*(tmpfr2-m));

        totalfreq = (i>>2) + m;
        if (var * totalpower > THRSH * height
        || var < 1.0e-30) continue;

        stdev = (float)sqrt((float)var);
        if (totalfreq < 4) totalfreq = 4;


        peaklist[*npeak].pwidth = stdev;
        peaklist[*npeak].ppow = height;
        peaklist[*npeak].ploudness = sqrt(sqrt(height));
        peaklist[*npeak].pfreq = totalfreq;
        (*npeak)++;
    }
}

void ptrack_pt3(int npeak, int numpks, PEAK *peaklist, float maxbin, float *histogram, float totalloudness, float partialonset[], int partialonset_count)
{
    int i, j, k;
    if (npeak > numpks) npeak = numpks;
    for (i = 0; i < maxbin; i++) histogram[i] = 0;
    for (i = 0; i < npeak; i++) {
        float pit = (float)(BPEROOVERLOG2 * logf(peaklist[i].pfreq) - 96.0);
        float binbandwidth = FACTORTOBINS * peaklist[i].pwidth/peaklist[i].pfreq;
        float putbandwidth = (binbandwidth < 2.0 ? 2.0 : binbandwidth);
        float weightbandwidth = (binbandwidth < 1.0 ? 1.0 : binbandwidth);
        float weightamp = 4.0 * peaklist[i].ploudness / totalloudness;
        for (j = 0; j < partialonset_count; j++) {
            float bin = pit - partialonset[j];
            if (bin < maxbin) {
                float para, pphase, score = 30.0 * weightamp /
                ((j+7) * weightbandwidth);
                int firstbin = bin + 0.5 - 0.5 * putbandwidth;
                int lastbin = bin + 0.5 + 0.5 * putbandwidth;
                int ibw = lastbin - firstbin;
                if (firstbin < -BINGUARD) break;
                para = 1.0 / (putbandwidth * putbandwidth);
                for (k = 0, pphase = firstbin-bin; k <= ibw; k++,pphase += 1.0) {
                    histogram[k+firstbin] += score * (1.0 - para * pphase * pphase);
                }
            }
        }
    }
}
