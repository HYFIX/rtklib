/*------------------------------------------------------------------------------
* ppp_ar.c : ppp ambiguity resolution
*
* reference :
*    [1] H.Okumura, C-gengo niyoru saishin algorithm jiten (in Japanese),
*        Software Technology, 1991
*
*          Copyright (C) 2012-2015 by T.TAKASU, All rights reserved.
*
* version : $Revision:$ $Date:$
* history : 2013/03/11  1.0  new
*           2016/05/10  1.1  delete codes
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

#define VAR_INT_PPP    1E-6
#define MIN_AMB_PPP    4

/* state indexes (kept in step with ppp.c) */
#define NF(opt)     ((opt)->ionoopt==IONOOPT_IFLC?1:(opt)->nf)
#define NP(opt)     ((opt)->dynamics?9:3)
#define NC(opt)     (NSYS)
#define NT(opt)     ((opt)->tropopt<TROPOPT_EST?0:((opt)->tropopt==TROPOPT_EST?1:3))
#define NI(opt)     ((opt)->ionoopt==IONOOPT_EST?MAXSAT:0)
#define ND(opt)     ((opt)->nf>=3?1:0)
#define NR(opt)     (NP(opt)+NC(opt)+NT(opt)+NI(opt)+ND(opt))
#define IB(s,f,opt) (NR(opt)+MAXSAT*(f)+(s)-1)

typedef struct {
    int sat,sys,code,f,ix,ref;
    double lam,el;
} pppamb_t;

/* covariance of two single-difference ambiguities in cycles */
static double cov_sd(const pppamb_t *a, const pppamb_t *b,
                     const pppamb_t *amb, const double *P, int nx)
{
    const pppamb_t *ar=amb+a->ref,*br=amb+b->ref;
    return P[a->ix+b->ix*nx]/a->lam/b->lam-
           P[a->ix+br->ix*nx]/a->lam/br->lam-
           P[ar->ix+b->ix*nx]/ar->lam/b->lam+
           P[ar->ix+br->ix*nx]/ar->lam/br->lam;
}

/* ambiguity resolution in ppp -----------------------------------------------*/
extern int ppp_ar(rtk_t *rtk, const obsd_t *obs, int n, int *exc,
                  const nav_t *nav, const double *azel, double *x, double *P)
{
    pppamb_t amb[MAXOBS*NFREQ];
    double *a,*Q,*F,*s,*H,*v,*R,ratio,thres;
    int i,j,k,f,na=0,nb=0,info;

    rtk->sol.ratio=0.0f;
    if (rtk->opt.modear==ARMODE_OFF||rtk->opt.ionoopt==IONOOPT_IFLC) return 0;

    /* Build only ambiguities whose observable-specific phase bias explicitly
     * carries the RTCM integer indicator. GLONASS FDMA is excluded because
     * its receiver inter-frequency bias does not cancel in satellite SDs. */
    for (i=0;i<n&&i<MAXOBS;i++) {
        int sat=obs[i].sat,sys=satsys(sat,NULL);
        const ssr_t *ssr=nav->ssr+sat-1;
        if (exc[i]||sys==SYS_GLO||!ssr->t0[5].time||
            fabs(timediff(obs[i].time,ssr->t0[5]))>=MAXAGESSR_BIAS) continue;
        for (f=0;f<NF(&rtk->opt)&&f<NFREQ;f++) {
            double freq;
            uint8_t code=obs[i].code[f];
            int ix=IB(sat,f,&rtk->opt);
            if (!code||!ssr->pbias_sii[code-1]||!rtk->ssat[sat-1].vsat[f]||
                rtk->ssat[sat-1].slip[f]||
                rtk->ssat[sat-1].lock[f]<rtk->opt.minlock||
                azel[i*2+1]<rtk->opt.elmaskar||x[ix]==0.0||P[ix+ix*rtk->nx]<=0.0||
                (freq=sat2freq(sat,code,nav))<=0.0) continue;
            amb[na].sat=sat; amb[na].sys=sys; amb[na].code=code;
            amb[na].f=f; amb[na].ix=ix; amb[na].ref=-1;
            amb[na].lam=CLIGHT/freq; amb[na].el=azel[i*2+1];
            na++;
        }
    }
    /* Highest-elevation reference for each constellation, frequency slot and
     * observation code. Keeping the code equal also cancels receiver OSBs. */
    for (i=0;i<na;i++) for (j=0;j<na;j++) {
        if (amb[j].sys!=amb[i].sys||amb[j].f!=amb[i].f||
            amb[j].code!=amb[i].code) continue;
        if (amb[i].ref<0||amb[j].el>amb[amb[i].ref].el) amb[i].ref=j;
    }
    for (i=0;i<na;i++) if (amb[i].ref>=0&&amb[i].ref!=i) nb++;
    if (nb<MIN_AMB_PPP) return 0;

    a=mat(nb,1); Q=mat(nb,nb); F=mat(nb,2); s=mat(2,1);
    H=zeros(rtk->nx,nb); v=mat(nb,1); R=zeros(nb,nb);
    for (i=k=0;i<na;i++) {
        const pppamb_t *r;
        if (amb[i].ref<0||amb[i].ref==i) continue;
        r=amb+amb[i].ref;
        a[k]=x[amb[i].ix]/amb[i].lam-x[r->ix]/r->lam;
        H[amb[i].ix+k*rtk->nx]= 1.0/amb[i].lam;
        H[r->ix     +k*rtk->nx]=-1.0/r->lam;
        for (j=0;j<na;j++) {
            int q=0,m;
            if (amb[j].ref<0||amb[j].ref==j) continue;
            for (m=0;m<j;m++) if (amb[m].ref>=0&&amb[m].ref!=m) q++;
            Q[k+q*nb]=cov_sd(amb+i,amb+j,amb,P,rtk->nx);
        }
        k++;
    }
    info=lambda(nb,2,a,Q,F,s);
    ratio=info?0.0:(s[0]<=0.0?999.9:s[1]/s[0]);
    if (ratio>999.9) ratio=999.9;
    rtk->sol.ratio=(float)ratio;
    thres=rtk->opt.thresar[0]>0.0?rtk->opt.thresar[0]:3.0;
    if (info||ratio<thres) {
        trace(3,"ppp_ar: validation failed n=%d ratio=%.2f\n",nb,ratio);
        free(a); free(Q); free(F); free(s); free(H); free(v); free(R);
        return 0;
    }
    for (i=0;i<nb;i++) {
        v[i]=F[i]-a[i];
        R[i+i*nb]=VAR_INT_PPP;
    }
    info=filter(x,P,H,v,R,rtk->nx,nb);
    if (!info) {
        for (i=0;i<na;i++) if (amb[i].ref>=0) {
            rtk->ssat[amb[i].sat-1].fix[amb[i].f]=2;
        }
        trace(3,"ppp_ar: validation ok n=%d ratio=%.2f\n",nb,ratio);
    }
    free(a); free(Q); free(F); free(s); free(H); free(v); free(R);
    return !info;
}
