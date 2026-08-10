/*------------------------------------------------------------------------------
* streamsvr.c : stream server functions
*
*          Copyright (C) 2010-2020 by T.TAKASU, All rights reserved.
*
* options : -DWIN32    use WIN32 API
*
* version : $Revision:$ $Date:$
* history : 2010/07/18 1.0  moved from stream.c
*           2011/01/18 1.1  change api strsvrstart()
*           2012/12/04 1.2  add stream conversion function
*           2012/12/25 1.3  fix bug on cyclic navigation data output
*                           suppress warnings
*           2013/05/08 1.4  fix bug on 1 s offset for javad -> rtcm conversion
*           2014/10/16 1.5  support input from stdout
*           2015/12/05 1.6  support rtcm 3 mt 63 beidou ephemeris
*           2016/07/23 1.7  change api strsvrstart(),strsvrstop()
*                           support command for output streams
*           2016/08/20 1.8  support api change of sendnmea()
*           2016/09/03 1.9  support ntrip caster function
*           2016/09/06 1.10 add api strsvrsetsrctbl()
*           2016/09/17 1.11 add relay back function of output stream
*                           fix bug on rtcm cyclic output of beidou ephemeris 
*           2016/10/01 1.12 change api startstrserver()
*           2017/04/11 1.13 fix bug on search of next satellite in nextsat()
*           2018/11/05 1.14 update message type of beidou ephemeirs
*                           support multiple msm messages if nsat x nsig > 64
*           2020/11/30 1.15 support RTCM MT1131-1137,1041 (NavIC/IRNSS)
*                           add log paths in API strsvrstart()
*                           add log status in API strsvrstat()
*                           support multiple ephemeris sets (e.g. I/NAV-F/NAV)
*                           delete API strsvrsetsrctbl()
*                           use integer types in stdint.h
*-----------------------------------------------------------------------------*/
#include "rtklib.h"

/* test observation data message ---------------------------------------------*/
static int is_obsmsg(int msg)
{
    return (1001<=msg&&msg<=1004)||(1009<=msg&&msg<=1012)||
           (1071<=msg&&msg<=1077)||(1081<=msg&&msg<=1087)||
           (1091<=msg&&msg<=1097)||(1101<=msg&&msg<=1107)||
           (1111<=msg&&msg<=1117)||(1121<=msg&&msg<=1127)||
           (1131<=msg&&msg<=1137);
}
/* test navigation data message ----------------------------------------------*/
static int is_navmsg(int msg)
{
    return msg==1019||msg==1020||msg==1044||msg==1045||msg==1046||
           msg==1042||msg==63  ||msg==1041;
}
/* test station info message -------------------------------------------------*/
static int is_stamsg(int msg)
{
    return msg==1005||msg==1006||msg==1007||msg==1008||msg==1033||msg==1230;
}
/* test time interval --------------------------------------------------------*/
static int is_tint(gtime_t time, double tint)
{
    if (tint<=0.0) return 1;
    return fmod(time2gpst(time,NULL)+DTTOL,tint)<=2.0*DTTOL;
}
/* new stream converter --------------------------------------------------------
* generate new stream converter
* args   : int    itype     I   input stream type  (STRFMT_???)
*          int    otype     I   output stream type (STRFMT_???)
*          char   *msgs     I   output message type and interval (, separated)
*          int    staid     I   station id
*          int    stasel    I   station info selection (0:remote,1:local)
*          char   *opt      I   rtcm or receiver raw options
* return : stream generator (NULL:error)
*-----------------------------------------------------------------------------*/
extern strconv_t *strconvnew(int itype, int otype, const char *msgs, int staid,
                             int stasel, const char *opt)
{
    strconv_t *conv;
    double tint;
    char buff[1024],*p;
    int msg;
    
    if (!(conv=(strconv_t *)malloc(sizeof(strconv_t)))) return NULL;
    
    conv->nmsg=0;
    strcpy(buff,msgs);
    for (p=strtok(buff,",");p;p=strtok(NULL,",")) {
       tint=0.0;
       if (sscanf(p,"%d(%lf)",&msg,&tint)<1) continue;
       conv->msgs[conv->nmsg]=msg;
       conv->tint[conv->nmsg]=tint;
       conv->tick[conv->nmsg]=tickget();
       conv->ephsat[conv->nmsg++]=0;
       if (conv->nmsg>=32) break;
    }
    if (conv->nmsg<=0) {
        free(conv);
        return NULL;
    }
    conv->itype=itype;
    conv->otype=otype;
    conv->stasel=stasel;
    if (!init_rtcm(&conv->rtcm)||!init_rtcm(&conv->out)) {
        free(conv);
        return NULL;
    }
    if (!init_raw(&conv->raw,itype)) {
        free_rtcm(&conv->rtcm);
        free_rtcm(&conv->out);
        free(conv);
        return NULL;
    }
    if (stasel) conv->out.staid=staid;
    sprintf(conv->rtcm.opt,"-EPHALL %s",opt);
    sprintf(conv->raw.opt ,"-EPHALL %s",opt);
    return conv;
}
/* free stream converter -------------------------------------------------------
* free stream converter
* args   : strconv_t *conv  IO  stream converter
* return : none
*-----------------------------------------------------------------------------*/
extern void strconvfree(strconv_t *conv)
{
    if (!conv) return;
    free_rtcm(&conv->rtcm);
    free_rtcm(&conv->out);
    free_raw(&conv->raw);
    free(conv);
}
/* copy received data from receiver raw to rtcm ------------------------------*/
static void raw2rtcm(rtcm_t *out, const raw_t *raw, int ret)
{
    int i,sat,set,sys,prn;
    
    out->time=raw->time;
    
    if (ret==1) {
        for (i=0;i<raw->obs.n;i++) {
            out->time=raw->obs.data[i].time;
            out->obs.data[i]=raw->obs.data[i];
            
            sys=satsys(raw->obs.data[i].sat,&prn);
            if (sys==SYS_GLO&&raw->nav.glo_fcn[prn-1]) {
                out->nav.glo_fcn[prn-1]=raw->nav.glo_fcn[prn-1];
            }
        }
        out->obs.n=raw->obs.n;
    }
    else if (ret==2) {
        sat=raw->ephsat;
        set=raw->ephset;
        sys=satsys(sat,&prn);
        if (sys==SYS_GLO) {
            out->nav.geph[prn-1]=raw->nav.geph[prn-1];
            out->ephsat=sat;
            out->ephset=set;
        }
        else if (sys==SYS_GPS||sys==SYS_GAL||sys==SYS_QZS||sys==SYS_CMP||
                 sys==SYS_IRN) {
            out->nav.eph[sat-1+MAXSAT*set]=raw->nav.eph[sat-1+MAXSAT*set];
            out->ephsat=sat;
            out->ephset=set;
        }
    }
    else if (ret==5) {
        out->sta=raw->sta;
    }
    else if (ret==9) {
        matcpy(out->nav.utc_gps,raw->nav.utc_gps,8,1);
        matcpy(out->nav.utc_glo,raw->nav.utc_glo,8,1);
        matcpy(out->nav.utc_gal,raw->nav.utc_gal,8,1);
        matcpy(out->nav.utc_qzs,raw->nav.utc_qzs,8,1);
        matcpy(out->nav.utc_cmp,raw->nav.utc_cmp,8,1);
        matcpy(out->nav.utc_irn,raw->nav.utc_irn,9,1);
        matcpy(out->nav.utc_sbs,raw->nav.utc_sbs,4,1);
        matcpy(out->nav.ion_gps,raw->nav.ion_gps,8,1);
        matcpy(out->nav.ion_gal,raw->nav.ion_gal,4,1);
        matcpy(out->nav.ion_qzs,raw->nav.ion_qzs,8,1);
        matcpy(out->nav.ion_cmp,raw->nav.ion_cmp,8,1);
        matcpy(out->nav.ion_irn,raw->nav.ion_irn,8,1);
    }
}
/* copy received data from receiver rtcm to rtcm -----------------------------*/
static void rtcm2rtcm(rtcm_t *out, const rtcm_t *rtcm, int ret, int stasel)
{
    int i,sat,set,sys,prn;
    
    out->time=rtcm->time;
    
    if (!stasel) out->staid=rtcm->staid;
    
    if (ret==1) {
        for (i=0;i<rtcm->obs.n;i++) {
            out->obs.data[i]=rtcm->obs.data[i];
            
            sys=satsys(rtcm->obs.data[i].sat,&prn);
            if (sys==SYS_GLO&&rtcm->nav.glo_fcn[prn-1]) {
                out->nav.glo_fcn[prn-1]=rtcm->nav.glo_fcn[prn-1];
            }
        }
        out->obs.n=rtcm->obs.n;
    }
    else if (ret==2) {
        sat=rtcm->ephsat;
        set=rtcm->ephset;
        sys=satsys(sat,&prn);
        if (sys==SYS_GLO) {
            out->nav.geph[prn-1]=rtcm->nav.geph[prn-1];
            out->ephsat=sat;
            out->ephset=set;
        }
        else if (sys==SYS_GPS||sys==SYS_GAL||sys==SYS_QZS||sys==SYS_CMP||
                 sys==SYS_IRN) {
            out->nav.eph[sat-1+MAXSAT*set]=rtcm->nav.eph[sat-1+MAXSAT*set];
            out->ephsat=sat;
            out->ephset=set;
        }
    }
    else if (ret==5) {
        if (!stasel) out->sta=rtcm->sta;
    }
}
/* write rtcm3 msm to stream -------------------------------------------------*/
static void write_rtcm3_msm(stream_t *str, rtcm_t *out, int msg, int sync)
{
    obsd_t *data,buff[MAXOBS];
    int i,j,n,ns,sys,nobs,code,nsat=0,nsig=0,nmsg,mask[MAXCODE]={0};
    
    if      (1071<=msg&&msg<=1077) sys=SYS_GPS;
    else if (1081<=msg&&msg<=1087) sys=SYS_GLO;
    else if (1091<=msg&&msg<=1097) sys=SYS_GAL;
    else if (1101<=msg&&msg<=1107) sys=SYS_SBS;
    else if (1111<=msg&&msg<=1117) sys=SYS_QZS;
    else if (1121<=msg&&msg<=1127) sys=SYS_CMP;
    else if (1131<=msg&&msg<=1137) sys=SYS_IRN;
    else return;
    
    data=out->obs.data;
    nobs=out->obs.n;
    
    /* count number of satellites and signals */
    for (i=0;i<nobs&&i<MAXOBS;i++) {
        if (satsys(data[i].sat,NULL)!=sys) continue;
        nsat++;
        for (j=0;j<NFREQ+NEXOBS;j++) {
            if (!(code=data[i].code[j])||mask[code-1]) continue;
            mask[code-1]=1;
            nsig++;
        }
    }
    if (nsig>64) return;
    
    /* pack data to multiple messages if nsat x nsig > 64 */
    if (nsig>0) {
        ns=64/nsig;         /* max number of sats in a message */
        nmsg=(nsat-1)/ns+1; /* number of messages */
    }
    else {
        ns=0;
        nmsg=1;
    }
    out->obs.data=buff;
    
    for (i=j=0;i<nmsg;i++) {
        for (n=0;n<ns&&j<nobs&&j<MAXOBS;j++) {
            if (satsys(data[j].sat,NULL)!=sys) continue;
            out->obs.data[n++]=data[j];
        }
        out->obs.n=n;
        
        if (gen_rtcm3(out,msg,0,i<nmsg-1?1:sync)) {
            strwrite(str,out->buff,out->nbyte);
        }
    }
    out->obs.data=data;
    out->obs.n=nobs;
}
/* write obs data messages ---------------------------------------------------*/
static void write_obs(gtime_t time, stream_t *str, strconv_t *conv)
{
    int i,j=0;
    
    for (i=0;i<conv->nmsg;i++) {
        if (!is_obsmsg(conv->msgs[i])||!is_tint(time,conv->tint[i])) continue;
        
        j=i; /* index of last message */
    }
    for (i=0;i<conv->nmsg;i++) {
        if (!is_obsmsg(conv->msgs[i])||!is_tint(time,conv->tint[i])) continue;
        
        /* generate messages */
        if (conv->otype==STRFMT_RTCM2) {
            if (!gen_rtcm2(&conv->out,conv->msgs[i],i!=j)) continue;
            
            /* write messages to stream */
            strwrite(str,conv->out.buff,conv->out.nbyte);
        }
        else if (conv->otype==STRFMT_RTCM3) {
            if (conv->msgs[i]<=1012) {
                if (!gen_rtcm3(&conv->out,conv->msgs[i],0,i!=j)) continue;
                strwrite(str,conv->out.buff,conv->out.nbyte);
            }
            else { /* write rtcm3 msm to stream */
                write_rtcm3_msm(str,&conv->out,conv->msgs[i],i!=j);
            }
        }
    }
}
/* write nav data messages ---------------------------------------------------*/
static void write_nav(gtime_t time, stream_t *str, strconv_t *conv)
{
    int i;
    
    for (i=0;i<conv->nmsg;i++) {
        if (!is_navmsg(conv->msgs[i])||conv->tint[i]>0.0) continue;
        
        /* generate messages */
        if (conv->otype==STRFMT_RTCM2) {
            if (!gen_rtcm2(&conv->out,conv->msgs[i],0)) continue;
        }
        else if (conv->otype==STRFMT_RTCM3) {
            if (!gen_rtcm3(&conv->out,conv->msgs[i],0,0)) continue;
        }
        else continue;
        
        /* write messages to stream */
        strwrite(str,conv->out.buff,conv->out.nbyte);
    }
}
/* next ephemeris satellite --------------------------------------------------*/
static int nextsat(nav_t *nav, int sat, int msg)
{
    int sys,set,p,p0,p1,p2;
    
    switch (msg) {
        case 1019: sys=SYS_GPS; set=0; p1=MINPRNGPS; p2=MAXPRNGPS; break;
        case 1020: sys=SYS_GLO; set=0; p1=MINPRNGLO; p2=MAXPRNGLO; break;
        case 1044: sys=SYS_QZS; set=0; p1=MINPRNQZS; p2=MAXPRNQZS; break;
        case 1045: sys=SYS_GAL; set=1; p1=MINPRNGAL; p2=MAXPRNGAL; break;
        case 1046: sys=SYS_GAL; set=0; p1=MINPRNGAL; p2=MAXPRNGAL; break;
        case   63:
        case 1042: sys=SYS_CMP; set=0; p1=MINPRNCMP; p2=MAXPRNCMP; break;
        case 1041: sys=SYS_IRN; set=0; p1=MINPRNIRN; p2=MAXPRNIRN; break;
        default: return 0;
    }
    if (satsys(sat,&p0)!=sys) return satno(sys,p1);
    
    /* search next valid ephemeris */
    for (p=p0>=p2?p1:p0+1;p!=p0;p=p>=p2?p1:p+1) {
        
        if (sys==SYS_GLO) {
            sat=satno(sys,p);
            if (nav->geph[p-1].sat==sat) return sat;
        }
        else {
            sat=satno(sys,p);
            if (nav->eph[sat-1+MAXSAT*set].sat==sat) return sat;
        }
    }
    return 0;
}
/* write cyclic nav data messages --------------------------------------------*/
static void write_nav_cycle(stream_t *str, strconv_t *conv)
{
    uint32_t tick=tickget();
    int i,sat,tint;
    
    for (i=0;i<conv->nmsg;i++) {
        if (!is_navmsg(conv->msgs[i])||conv->tint[i]<=0.0) continue;
        
        /* output cycle */
        tint=(int)(conv->tint[i]*1000.0);
        if ((int)(tick-conv->tick[i])<tint) continue;
        conv->tick[i]=tick;
        
        /* next satellite */
        if (!(sat=nextsat(&conv->out.nav,conv->ephsat[i],conv->msgs[i]))) {
            continue;
        }
        conv->out.ephsat=conv->ephsat[i]=sat;
        
        /* generate messages */
        if (conv->otype==STRFMT_RTCM2) {
            if (!gen_rtcm2(&conv->out,conv->msgs[i],0)) continue;
        }
        else if (conv->otype==STRFMT_RTCM3) {
            if (!gen_rtcm3(&conv->out,conv->msgs[i],0,0)) continue;
        }
        else continue;
        
        /* write messages to stream */
        strwrite(str,conv->out.buff,conv->out.nbyte);
    }
}
/* write cyclic station info messages ----------------------------------------*/
static void write_sta_cycle(stream_t *str, strconv_t *conv)
{
    uint32_t tick=tickget();
    int i,tint;
    
    for (i=0;i<conv->nmsg;i++) {
        if (!is_stamsg(conv->msgs[i])) continue;
        
        /* output cycle */
        tint=conv->tint[i]==0.0?30000:(int)(conv->tint[i]*1000.0);
        if ((int)(tick-conv->tick[i])<tint) continue;
        conv->tick[i]=tick;
        
        /* generate messages */
        if (conv->otype==STRFMT_RTCM2) {
            if (!gen_rtcm2(&conv->out,conv->msgs[i],0)) continue;
        }
        else if (conv->otype==STRFMT_RTCM3) {
            if (!gen_rtcm3(&conv->out,conv->msgs[i],0,0)) continue;
        }
        else continue;
        
        /* write messages to stream */
        strwrite(str,conv->out.buff,conv->out.nbyte);
    }
}
/* update ROTI (Rate of TEC Index) -------------------------------------------*/
static void update_roti(strsvr_t *svr, const obs_t *obs, const nav_t *nav)
{
    gtime_t time;
    double c=299792458.0;
    double f1,f2,lam1,lam2,phi_gf,tec,dt;
    int i,j,f,sat,sys,prn,idx,f1_idx,f2_idx;
    char sat_id[32];
    char out_str[4096]="";
    char *p=out_str;
    
    if (obs->n<=0) return;
    time=obs->data[0].time;
    
    for (i=0;i<obs->n;i++) {
        sat=obs->data[i].sat;
        sys=satsys(sat,&prn);
        
        f1_idx=-1; f2_idx=-1;
        for (f=0;f<NFREQ;f++) {
            if (obs->data[i].L[f]!=0.0&&obs->data[i].code[f]!=CODE_NONE) {
                if (f1_idx==-1) f1_idx=f;
                else if (f2_idx==-1) { f2_idx=f; break; }
            }
        }
        if (f1_idx==-1||f2_idx==-1) continue;
        
        f1=sat2freq(sat,obs->data[i].code[f1_idx],nav);
        f2=sat2freq(sat,obs->data[i].code[f2_idx],nav);
        if (f1<=0.0||f2<=0.0) continue;
        
        lam1=c/f1;
        lam2=c/f2;
        
        phi_gf=lam1*obs->data[i].L[f1_idx]-lam2*obs->data[i].L[f2_idx];
        
        tec=phi_gf/(40.3e16*(1.0/(f2*f2)-1.0/(f1*f1)));
        
        idx=sat-1;
        if (idx<0||idx>=MAXSAT) continue;
        roti_sat_t *rs=&svr->roti_sat[idx];
        
        /* cycle slip check */
        if ((obs->data[i].LLI[f1_idx]&1)||(obs->data[i].LLI[f2_idx]&1)) {
            rs->n=0;
        }
        else if (rs->n>0) {
            int last_idx=(rs->head+rs->n-1)%300;
            if (fabs(tec-rs->tec[last_idx])>5.0) {
                rs->n=0;
            }
        }
        
        /* add to circular buffer */
        int next_idx=(rs->head+rs->n)%300;
        rs->time[next_idx]=obs->data[i].time;
        rs->tec[next_idx]=tec;
        if (rs->n<300) {
            rs->n++;
        }
        else {
            rs->head=(rs->head+1)%300;
        }
        
        /* remove points older than 5 minutes */
        while (rs->n>0) {
            dt=timediff(obs->data[i].time,rs->time[rs->head]);
            if (dt>300.0) {
                rs->head=(rs->head+1)%300;
                rs->n--;
            }
            else {
                break;
            }
        }
        
        /* compute ROTI if we have enough points */
        if (rs->n>=10) {
            double sum_rot=0.0,sum_rot2=0.0;
            int count=0;
            for (j=0;j<rs->n-1;j++) {
                int idx1=(rs->head+j)%300;
                int idx2=(rs->head+j+1)%300;
                double t_diff=timediff(rs->time[idx2],rs->time[idx1]);
                if (t_diff>0.0&&t_diff<60.0) {
                    double rot=(rs->tec[idx2]-rs->tec[idx1])/t_diff*60.0; /* TECU/min */
                    sum_rot+=rot;
                    sum_rot2+=rot*rot;
                    count++;
                }
            }
            if (count>=9) {
                double mean_rot=sum_rot/count;
                double var_rot=sum_rot2/count-mean_rot*mean_rot;
                double roti=var_rot>0.0?sqrt(var_rot):0.0;
                
                satno2id(sat,sat_id);
                p+=sprintf(p," %s=%.2f",sat_id,roti);
            }
        }
    }
}

/* update pseudorange multipath stats (MP12/MP21) based on TEQC method -------*/
static void update_mp(strsvr_t *svr, const obs_t *obs, const nav_t *nav)
{
    gtime_t time;
    double c=299792458.0;
    double f1,f2,lam1,lam2,alpha,phi1,phi2,p1,p2,mp12,mp21,tec;
    int i,f,sat,sys,prn,idx,slip,f1_idx,f2_idx;
    char sat_id[32];
    char out_str1[4096]="";
    char out_str2[4096]="";
    char *p1_str=out_str1;
    char *p2_str=out_str2;
    
    if (obs->n<=0) return;
    time=obs->data[0].time;
    
    for (i=0;i<obs->n;i++) {
        sat=obs->data[i].sat;
        sys=satsys(sat,&prn);
        
        f1_idx=-1; f2_idx=-1;
        for (f=0;f<NFREQ;f++) {
            if (obs->data[i].L[f]!=0.0&&obs->data[i].P[f]!=0.0&&obs->data[i].code[f]!=CODE_NONE) {
                if (f1_idx==-1) f1_idx=f;
                else if (f2_idx==-1) { f2_idx=f; break; }
            }
        }
        if (f1_idx==-1||f2_idx==-1) continue;
        
        f1=sat2freq(sat,obs->data[i].code[f1_idx],nav);
        f2=sat2freq(sat,obs->data[i].code[f2_idx],nav);
        if (f1<=0.0||f2<=0.0) continue;
        
        lam1=c/f1;
        lam2=c/f2;
        alpha=(f1*f1)/(f2*f2);
        
        phi1=lam1*obs->data[i].L[f1_idx];
        phi2=lam2*obs->data[i].L[f2_idx];
        p1=obs->data[i].P[f1_idx];
        p2=obs->data[i].P[f2_idx];
        
        /* MP12 and MP21 formulas based on TEQC */
        mp12=p1-(1.0+2.0/(alpha-1.0))*phi1+(2.0/(alpha-1.0))*phi2;
        mp21=p2-(2.0*alpha/(alpha-1.0))*phi1+(2.0*alpha/(alpha-1.0)-1.0)*phi2;
        
        /* TEC for sudden jump check */
        tec=(phi1-phi2)/(40.3e16*(1.0/(f2*f2)-1.0/(f1*f1)));
        
        idx=sat-1;
        if (idx<0||idx>=MAXSAT) continue;
        mp_sat_t *ms=&svr->mp_sat[idx];
        
        /* cycle slip check */
        slip=0;
        if (obs->data[i].LLI[f1_idx]&1) {
            ms->mp1_cs++;
            slip=1;
        }
        if (obs->data[i].LLI[f2_idx]&1) {
            ms->mp2_cs++;
            slip=1;
        }
        if (ms->n>0) {
            if (fabs(tec-ms->last_tec)>5.0) {
                ms->iod_cs++;
                slip=1;
            }
        }
        
        if (slip) {
            /* commit completed arc before reset */
            if (ms->n>=2) {
                ms->accum_mp12+=(ms->sum2_mp12-ms->sum_mp12*ms->sum_mp12/ms->n);
                ms->accum_mp21+=(ms->sum2_mp21-ms->sum_mp21*ms->sum_mp21/ms->n);
                ms->total_n+=ms->n;
            }
            ms->n=0;
            ms->sum_mp12=ms->sum2_mp12=0.0;
            ms->sum_mp21=ms->sum2_mp21=0.0;
        }
        
        ms->last_tec=tec;
        ms->n_valid++;
        ms->code1=obs->data[i].code[f1_idx];
        ms->code2=obs->data[i].code[f2_idx];
        
        /* update stats */
        ms->n++;
        ms->sum_mp12+=mp12;
        ms->sum2_mp12+=mp12*mp12;
        ms->sum_mp21+=mp21;
        ms->sum2_mp21+=mp21*mp21;
        
        if (ms->n>=10) {
            double mean12=ms->sum_mp12/ms->n;
            double var12=ms->sum2_mp12/ms->n-mean12*mean12;
            double rms12=var12>0.0?sqrt(var12):0.0;
            
            double mean21=ms->sum_mp21/ms->n;
            double var21=ms->sum2_mp21/ms->n-mean21*mean21;
            double rms21=var21>0.0?sqrt(var21):0.0;
            
            satno2id(sat,sat_id);
            p1_str+=sprintf(p1_str," %s=%.2f",sat_id,rms12);
            p2_str+=sprintf(p2_str," %s=%.2f",sat_id,rms21);
        }
    }
}

/* print periodic ROTI report ------------------------------------------------*/
extern void print_roti_report(strsvr_t *svr)
{
    int i,j,sat,sys,prn;
    char sat_id[32];
    const char *sys_names[]={"GPS","GLO","GAL","BDS","QZS","SBS"};
    int sys_map[]={SYS_GPS,SYS_GLO,SYS_GAL,SYS_CMP,SYS_QZS,SYS_SBS};
    char out_buf[16384]="";
    char *p=out_buf;
    char tstr[40];
    gtime_t now=timeget();
    time2str(utc2gpst(now),tstr,0);
    
    p+=sprintf(p,"\n=== ROTI Report [%s] ===\n",tstr);
    p+=sprintf(p,"%-6s  %5s  ROTI(TECU/min)  Window\n","PRN","Sys");
    p+=sprintf(p,"------  -----  -------------  ------\n");
    
    for (sys=0;sys<4;sys++) {
        double sum_roti=0.0;
        int count=0;
        
        for (i=0;i<MAXSAT;i++) {
            sat=i+1;
            if (satsys(sat,&prn)!=sys_map[sys]) continue;
            roti_sat_t *rs=&svr->roti_sat[i];
            if (rs->n<10) continue;
            
            /* compute ROTI for this satellite */
            double sum_rot=0.0,sum_rot2=0.0;
            int cnt=0;
            for (j=0;j<rs->n-1;j++) {
                int idx1=(rs->head+j)%300;
                int idx2=(rs->head+j+1)%300;
                double t_diff=timediff(rs->time[idx2],rs->time[idx1]);
                if (t_diff>0.0&&t_diff<60.0) {
                    double rot=(rs->tec[idx2]-rs->tec[idx1])/t_diff*60.0;
                    sum_rot+=rot;
                    sum_rot2+=rot*rot;
                    cnt++;
                }
            }
            if (cnt<9) continue;
            double mean_rot=sum_rot/cnt;
            double var_rot=sum_rot2/cnt-mean_rot*mean_rot;
            double roti=var_rot>0.0?sqrt(var_rot):0.0;
            
            /* time window covered */
            double window=rs->n>1 ? timediff(rs->time[(rs->head+rs->n-1)%300],rs->time[rs->head]) : 0.0;
            
            satno2id(sat,sat_id);
            p+=sprintf(p,"%-6s  %-5s  %13.3f  %4.0fs\n",sat_id,sys_names[sys],roti,window);
            sum_roti+=roti;
            count++;
        }
        if (count>0) {
            p+=sprintf(p,"  --> %-3s avg ROTI: %.3f TECU/min (%d sats)\n",
                       sys_names[sys],sum_roti/count,count);
        }
    }
    p+=sprintf(p,"\n");
    
    if (svr->roti==1) {
        printf("%s",out_buf);
        fflush(stdout);
    }
    else if (svr->roti==2) {
        fprintf(stderr,"%s",out_buf);
    }
}

/* print periodic MP report (non-destructive, uses snapshot of current state) */
extern void print_mp_report(strsvr_t *svr)
{
    int i,sat,sys,prn;
    const char *sys_names[]={"GPS","GLO","GAL","BDS","QZS","SBS"};
    int sys_map[]={SYS_GPS,SYS_GLO,SYS_GAL,SYS_CMP,SYS_QZS,SYS_SBS};
    char out_buf[32768]="";
    char *p=out_buf;
    char tstr[40],sig1_str[8],sig2_str[8];
    gtime_t now=timeget();
    time2str(utc2gpst(now),tstr,0);
    
    p+=sprintf(p,"\n=== MP Report [%s] ===\n",tstr);
    p+=sprintf(p,"Sys Sig1 Sig2  PRN   MP1(m)   MP2(m)  IOD_CS  MP1_CS  MP2_CS  ValidObs  O/Slips\n");
    p+=sprintf(p,"--- ---- ----  ---  --------  --------  ------  ------  ------  --------  -------\n");
    
    for (sys=0;sys<4;sys++) {
        int has_obs=0;
        int tot_n_valid=0,tot_iod_cs=0,tot_mp1_cs=0,tot_mp2_cs=0,tot_n_accum=0;
        double tot_accum_mp12=0.0,tot_accum_mp21=0.0;
        uint8_t last_code1=0,last_code2=0;
        
        for (i=0;i<MAXSAT;i++) {
            sat=i+1;
            if (satsys(sat,&prn)!=sys_map[sys]) continue;
            mp_sat_t *ms=&svr->mp_sat[i];
            if (ms->n_valid<=0) continue;
            has_obs=1;
            
            /* snapshot: include current open arc without committing */
            double snap_accum12=ms->accum_mp12;
            double snap_accum21=ms->accum_mp21;
            int snap_total_n=ms->total_n;
            if (ms->n>=2) {
                snap_accum12+=(ms->sum2_mp12-ms->sum_mp12*ms->sum_mp12/ms->n);
                snap_accum21+=(ms->sum2_mp21-ms->sum_mp21*ms->sum_mp21/ms->n);
                snap_total_n+=ms->n;
            }
            
            double rms12=snap_total_n>0 ? sqrt(snap_accum12/snap_total_n) : 0.0;
            double rms21=snap_total_n>0 ? sqrt(snap_accum21/snap_total_n) : 0.0;
            int sat_cs=ms->iod_cs+ms->mp1_cs+ms->mp2_cs;
            double o_slips=sat_cs>0 ? (double)ms->n_valid/sat_cs : (double)ms->n_valid;
            
            strcpy(sig1_str,code2obs(ms->code1));
            strcpy(sig2_str,code2obs(ms->code2));
            
            p+=sprintf(p,"%-3s %-4s %-4s  %03d  %8.4f  %8.4f  %6d  %6d  %6d  %8d  %7.1f\n",
                       sys_names[sys],sig1_str,sig2_str,prn,
                       rms12,rms21,
                       ms->iod_cs,ms->mp1_cs,ms->mp2_cs,
                       ms->n_valid,o_slips);
            
            tot_n_valid+=ms->n_valid;
            tot_iod_cs+=ms->iod_cs;
            tot_mp1_cs+=ms->mp1_cs;
            tot_mp2_cs+=ms->mp2_cs;
            tot_accum_mp12+=snap_accum12;
            tot_accum_mp21+=snap_accum21;
            tot_n_accum+=snap_total_n;
            if (ms->code1>0) last_code1=ms->code1;
            if (ms->code2>0) last_code2=ms->code2;
        }
        if (!has_obs) continue;
        
        double rms12_c=tot_n_accum>0 ? sqrt(tot_accum_mp12/tot_n_accum) : 0.0;
        double rms21_c=tot_n_accum>0 ? sqrt(tot_accum_mp21/tot_n_accum) : 0.0;
        int tot_cs=tot_iod_cs+tot_mp1_cs+tot_mp2_cs;
        double o_slips_c=tot_cs>0 ? (double)tot_n_valid/tot_cs : (double)tot_n_valid;
        strcpy(sig1_str,code2obs(last_code1));
        strcpy(sig2_str,code2obs(last_code2));
        p+=sprintf(p,"--- ---- ----  ---  --------  --------  ------  ------  ------  --------  -------\n");
        p+=sprintf(p,"%-3s %-4s %-4s  AVG  %8.4f  %8.4f  %6d  %6d  %6d  %8d  %7.1f\n\n",
                   sys_names[sys],sig1_str,sig2_str,
                   rms12_c,rms21_c,
                   tot_iod_cs,tot_mp1_cs,tot_mp2_cs,
                   tot_n_valid,o_slips_c);
    }
    
    if (svr->mp==1) {
        printf("%s",out_buf);
        fflush(stdout);
    }
    else if (svr->mp==2) {
        fprintf(stderr,"%s",out_buf);
    }
}

/* convert stearm ------------------------------------------------------------*/
static void strconv(strsvr_t *svr, stream_t *str, strconv_t *conv, uint8_t *buff, int n)
{
    int i,ret;
    
    for (i=0;i<n;i++) {
        
        /* input rtcm 2 messages */
        if (conv->itype==STRFMT_RTCM2) {
            ret=input_rtcm2(&conv->rtcm,buff[i]);
            rtcm2rtcm(&conv->out,&conv->rtcm,ret,conv->stasel);
        }
        /* input rtcm 3 messages */
        else if (conv->itype==STRFMT_RTCM3) {
            ret=input_rtcm3(&conv->rtcm,buff[i]);
            rtcm2rtcm(&conv->out,&conv->rtcm,ret,conv->stasel);
        }
        /* input receiver raw messages */
        else {
            ret=input_raw(&conv->raw,conv->itype,buff[i]);
            raw2rtcm(&conv->out,&conv->raw,ret);
        }
        /* write obs and nav data messages to stream */
        switch (ret) {
            case 1:
                write_obs(conv->out.time,str,conv);
                if (svr->roti) {
                    update_roti(svr,&conv->out.obs,&conv->out.nav);
                }
                if (svr->mp) {
                    update_mp(svr,&conv->out.obs,&conv->out.nav);
                }
                break;
            case 2: write_nav(conv->out.time,str,conv); break;
        }
    }
    /* write cyclic nav data and station info messages to stream */
    write_nav_cycle(str,conv);
    write_sta_cycle(str,conv);
}
/* periodic command ----------------------------------------------------------*/
static void periodic_cmd(int cycle, const char *cmd, stream_t *stream)
{
    const char *p=cmd,*q;
    char msg[1024],*r;
    int n,period;
    
    for (p=cmd;;p=q+1) {
        for (q=p;;q++) if (*q=='\r'||*q=='\n'||*q=='\0') break;
        n=(int)(q-p); strncpy(msg,p,n); msg[n]='\0';
        
        period=0;
        if ((r=strrchr(msg,'#'))) {
            sscanf(r,"# %d",&period);
            *r='\0';
            while (*--r==' ') *r='\0'; /* delete tail spaces */
        }
        if (period<=0) period=1000;
        if (*msg&&cycle%period==0) {
            strsendcmd(stream,msg);
        }
        if (!*q) break;
    }
}
/* stearm server thread ------------------------------------------------------*/
#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI strsvrthread(void *arg)
#else
static void *strsvrthread(void *arg)
#endif
{
    strsvr_t *svr=(strsvr_t *)arg;
    sol_t sol_nmea={{0}};
    uint32_t tick,tick_nmea;
    uint8_t buff[1024];
    int i,n,cyc;
    
    tracet(3,"strsvrthread:\n");
    
    svr->tick=tickget();
    tick_nmea=svr->tick-1000;
    
    for (cyc=0;svr->state;cyc++) {
        tick=tickget();
        
        /* read data from input stream */
        while ((n=strread(svr->stream,svr->buff,svr->buffsize))>0&&svr->state) {
            
            /* decode RTCM if option enabled */
            if (svr->rtcmdec) {
                int k;
                for (k=0;k<n;k++) {
                    svr->rtcm.msgtype[0]='\0';
                    if (svr->rtcmdec_fmt==STRFMT_RTCM2) {
                        input_rtcm2(&svr->rtcm,svr->buff[k]);
                    }
                    else {
                        input_rtcm3(&svr->rtcm,svr->buff[k]);
                    }
                    if (svr->rtcm.msgtype[0]!='\0') {
                        if (svr->rtcmdec==1) { /* stdout */
                            printf("%s\n",svr->rtcm.msgtype);
                            fflush(stdout);
                        }
                        else if (svr->rtcmdec==2) { /* stderr */
                            fprintf(stderr,"%s\n",svr->rtcm.msgtype);
                        }
                    }
                }
            }
            
            /* write data to output streams */
            for (i=1;i<svr->nstr;i++) {
                if (svr->conv[i-1]) {
                    strconv(svr,svr->stream+i,svr->conv[i-1],svr->buff,n);
                }
                else {
                    strwrite(svr->stream+i,svr->buff,n);
                }
            }
            /* write data to log stream */
            strwrite(svr->strlog,svr->buff,n);
            
            lock_(&svr->lock);
            for (i=0;i<n&&svr->npb<svr->buffsize;i++) {
                svr->pbuf[svr->npb++]=svr->buff[i];
            }
            unlock_(&svr->lock);
        }
        for (i=1;i<svr->nstr;i++) {
            
            /* read message from output stream */
            while ((n=strread(svr->stream+i,buff,sizeof(buff)))>0) {
                
                /* relay back message from output stream to input stream */
                if (i==svr->relayback) {
                    strwrite(svr->stream,buff,n);
                }
                /* write data to log stream */
                strwrite(svr->strlog+i,buff,n);
            }
        }
        /* write periodic command to input stream */
        for (i=0;i<svr->nstr;i++) {
            periodic_cmd(cyc*svr->cycle,svr->cmds_periodic[i],svr->stream+i);
        }
        /* write nmea messages to input stream */
        if (svr->nmeacycle>0&&(int)(tick-tick_nmea)>=svr->nmeacycle) {
            sol_nmea.stat=SOLQ_SINGLE;
            sol_nmea.time=utc2gpst(timeget());
            matcpy(sol_nmea.rr,svr->nmeapos,3,1);
            strsendnmea(svr->stream,&sol_nmea);
            tick_nmea=tick;
        }
        /* periodic ROTI/MP reports */
        if (svr->rpt_interval>0) {
            uint32_t now_tick=tickget();
            if (svr->last_rpt_tick==0) svr->last_rpt_tick=now_tick;
            if ((int)(now_tick-svr->last_rpt_tick)>=(int)(svr->rpt_interval*1000)) {
                if (svr->roti) print_roti_report(svr);
                if (svr->mp)   print_mp_report(svr);
                svr->last_rpt_tick=now_tick;
            }
        }
        sleepms(svr->cycle-(int)(tickget()-tick));
    }
    for (i=0;i<svr->nstr;i++) strclose(svr->stream+i);
    for (i=0;i<svr->nstr;i++) strclose(svr->strlog+i);
    svr->npb=0;
    free(svr->buff); svr->buff=NULL;
    free(svr->pbuf); svr->pbuf=NULL;
    
    return 0;
}
/* initialize stream server ----------------------------------------------------
* initialize stream server
* args   : strsvr_t *svr    IO  stream sever struct
*          int    nout      I   number of output streams
* return : none
*-----------------------------------------------------------------------------*/
extern void strsvrinit(strsvr_t *svr, int nout)
{
    int i;
    
    tracet(3,"strsvrinit: nout=%d\n",nout);
    
    svr->state=0;
    svr->cycle=0;
    svr->buffsize=0;
    svr->nmeacycle=0;
    svr->relayback=0;
    svr->npb=0;
    for (i=0;i<16;i++) *svr->cmds_periodic[i]='\0';
    for (i=0;i<3;i++) svr->nmeapos[i]=0.0;
    svr->buff=svr->pbuf=NULL;
    svr->tick=0;
    for (i=0;i<nout+1&&i<16;i++) strinit(svr->stream+i);
    for (i=0;i<nout+1&&i<16;i++) strinit(svr->strlog+i);
    svr->nstr=i;
    for (i=0;i<16;i++) svr->conv[i]=NULL;
    svr->thread=0;
    svr->rtcmdec=0;
    svr->rtcmdec_fmt=0;
    svr->roti=0;
    svr->mp=0;
    svr->rpt_interval=60;
    svr->last_rpt_tick=0;
    memset(svr->roti_sat,0,sizeof(svr->roti_sat));
    memset(svr->mp_sat,0,sizeof(svr->mp_sat));
    memset(&svr->rtcm,0,sizeof(rtcm_t));
    initlock(&svr->lock);
}
/* start stream server ---------------------------------------------------------
* start stream server
* args   : strsvr_t *svr    IO  stream sever struct
*          int    *opts     I   stream options
*              opts[0]= inactive timeout (ms)
*              opts[1]= interval to reconnect (ms)
*              opts[2]= averaging time of data rate (ms)
*              opts[3]= receive/send buffer size (bytes);
*              opts[4]= server cycle (ms)
*              opts[5]= nmea request cycle (ms) (0:no)
*              opts[6]= file swap margin (s)
*              opts[7]= relay back of output stream (0:no)
*          int    *strs     I   stream types (STR_???)
*              strs[0]= input stream
*              strs[1]= output stream 1
*              strs[2]= output stream 2
*              strs[3]= output stream 3
*              ...
*          char   **paths   I   stream paths
*              paths[0]= input stream
*              paths[1]= output stream 1
*              paths[2]= output stream 2
*              paths[3]= output stream 3
*              ...
*          char   **logs    I   log paths
*              logs[0]= input log path
*              logs[1]= output stream 1 return log path
*              logs[2]= output stream 2 retrun log path
*              logs[3]= output stream 2 retrun log path
*              ...
*          strconv_t **conv I   stream converter
*              conv[0]= output stream 1 converter
*              conv[1]= output stream 2 converter
*              conv[2]= output stream 3 converter
*              ...
*          char   **cmds    I   start/stop commands (NULL: no cmd)
*              cmds[0]= input stream command
*              cmds[1]= output stream 1 command
*              cmds[2]= output stream 2 command
*              cmds[3]= output stream 3 command
*              ...
*          char   **cmds_periodic I periodic commands (NULL: no cmd)
*              cmds[0]= input stream command
*              cmds[1]= output stream 1 command
*              cmds[2]= output stream 2 command
*              cmds[3]= output stream 3 command
*              ...
*          double *nmeapos  I   nmea request position (ecef) (m) (NULL: no)
* return : status (0:error,1:ok)
*-----------------------------------------------------------------------------*/
extern int strsvrstart(strsvr_t *svr, int *opts, int *strs, char **paths,
                       char **logs, strconv_t **conv, char **cmds,
                       char **cmds_periodic, const double *nmeapos)
{
    int i,rw,stropt[5]={0};
    char file1[MAXSTRPATH],file2[MAXSTRPATH],*p;
    
    tracet(3,"strsvrstart:\n");
    
    if (svr->state) return 0;
    
    strinitcom();
    
    for (i=0;i<4;i++) stropt[i]=opts[i];
    stropt[4]=opts[6];
    strsetopt(stropt);
    svr->cycle=opts[4];
    svr->buffsize=opts[3]<4096?4096:opts[3]; /* >=4096byte */
    svr->nmeacycle=0<opts[5]&&opts[5]<1000?1000:opts[5]; /* >=1s */
    svr->relayback=opts[7];
    for (i=0;i<3;i++) svr->nmeapos[i]=nmeapos?nmeapos[i]:0.0;
    for (i=0;i<svr->nstr;i++) {
        strcpy(svr->cmds_periodic[i],!cmds_periodic[i]?"":cmds_periodic[i]);
    }
    for (i=0;i<svr->nstr-1;i++) svr->conv[i]=conv[i];
    
    if (!(svr->buff=(uint8_t *)malloc(svr->buffsize))||
        !(svr->pbuf=(uint8_t *)malloc(svr->buffsize))) {
        free(svr->buff); free(svr->pbuf);
        return 0;
    }
    /* open streams */
    for (i=0;i<svr->nstr;i++) {
        strcpy(file1,paths[0]); if ((p=strstr(file1,"::"))) *p='\0';
        strcpy(file2,paths[i]); if ((p=strstr(file2,"::"))) *p='\0';
        if (i>0&&*file1&&!strcmp(file1,file2)) {
            sprintf(svr->stream[i].msg,"output path error: %-512.512s",file2);
            for (i--;i>=0;i--) strclose(svr->stream+i);
            return 0;
        }
        if (strs[i]==STR_FILE) {
            rw=i==0?STR_MODE_R:STR_MODE_W;
        }
        else {
            rw=STR_MODE_RW;
        }
        if (stropen(svr->stream+i,strs[i],rw,paths[i])) continue;
        for (i--;i>=0;i--) strclose(svr->stream+i);
        return 0;
    }
    /* open log streams */
    for (i=0;i<svr->nstr;i++) {
        if (strs[i]==STR_NONE||strs[i]==STR_FILE||!*logs[i]) continue;
        stropen(svr->strlog+i,STR_FILE,STR_MODE_W,logs[i]);
    }
    /* write start commands to input/output streams */
    for (i=0;i<svr->nstr;i++) {
        if (!cmds[i]) continue;
        strwrite(svr->stream+i,(uint8_t *)"",0); /* for connect */
        sleepms(100);
        strsendcmd(svr->stream+i,cmds[i]);
    }
    svr->state=1;
    
    if (svr->rtcmdec) {
        init_rtcm(&svr->rtcm);
        svr->rtcm.outtype=1;
    }
    
    /* create stream server thread */
#if defined(_WIN32) || defined(WIN32)
    if (!(svr->thread=CreateThread(NULL,0,strsvrthread,svr,0,NULL))) {
#else
    if (pthread_create(&svr->thread,NULL,strsvrthread,svr)) {
#endif
        for (i=0;i<svr->nstr;i++) strclose(svr->stream+i);
        svr->state=0;
        return 0;
    }
    return 1;
}
/* stop stream server ----------------------------------------------------------
* start stream server
* args   : strsvr_t *svr    IO  stream server struct
*          char  **cmds     I   stop commands (NULL: no cmd)
*              cmds[0]= input stream command
*              cmds[1]= output stream 1 command
*              cmds[2]= output stream 2 command
*              cmds[3]= output stream 3 command
*              ...
* return : none
*-----------------------------------------------------------------------------*/
extern void strsvrstop(strsvr_t *svr, char **cmds)
{
    int i;
    
    tracet(3,"strsvrstop:\n");
    
    for (i=0;i<svr->nstr;i++) {
        if (cmds[i]) strsendcmd(svr->stream+i,cmds[i]);
    }
    svr->state=0;
    
#if defined(_WIN32) || defined(WIN32)
    WaitForSingleObject(svr->thread,10000);
    CloseHandle(svr->thread);
#else
    pthread_join(svr->thread,NULL);
#endif

    if (svr->rtcmdec) {
        free_rtcm(&svr->rtcm);
    }
}
/* get stream server status ----------------------------------------------------
* get status of stream server
* args   : strsvr_t *svr    IO  stream sever struct
*          int    *stat     O   stream status
*          int    *log_stat O   log status
*          int    *byte     O   bytes received/sent
*          int    *bps      O   bitrate received/sent
*          char   *msg      O   messages
* return : none
*-----------------------------------------------------------------------------*/
extern void strsvrstat(strsvr_t *svr, int *stat, int *log_stat, int *byte,
                       int *bps, char *msg)
{
    char s[MAXSTRMSG]="",*p=msg;
    int i,bps_in;
    
    tracet(4,"strsvrstat:\n");
    
    for (i=0;i<svr->nstr;i++) {
        if (i==0) {
            strsum(svr->stream,byte,bps,NULL,NULL);
        }
        else {
            strsum(svr->stream+i,NULL,&bps_in,byte+i,bps+i);
        }
        stat[i]=strstat(svr->stream+i,s);
        if (*s) p+=sprintf(p,"(%d) %s ",i,s);
        log_stat[i]=strstat(svr->strlog+i,s);
    }
}
/* peek input/output stream ----------------------------------------------------
* peek input/output stream of stream server
* args   : strsvr_t *svr    IO  stream sever struct
*          uint8_t *buff    O   stream buff
*          int    nmax      I   buffer size (bytes)
* return : stream size (bytes)
*-----------------------------------------------------------------------------*/
extern int strsvrpeek(strsvr_t *svr, uint8_t *buff, int nmax)
{
    int n;
    
    if (!svr->state) return 0;
    
    lock_(&svr->lock);
    n=svr->npb<nmax?svr->npb:nmax;
    if (n>0) {
        memcpy(buff,svr->pbuf,n);
    }
    if (n<svr->npb) {
        memmove(svr->pbuf,svr->pbuf+n,svr->npb-n);
    }
    svr->npb-=n;
    unlock_(&svr->lock);
    return n;
}

/* print TEQC-compatible multipath summary table -------------------------------*/
extern void print_teqc_summary(strsvr_t *svr)
{
    int i,sat,sys,prn;
    int total_n_valid[6]={0};
    int total_iod_cs[6]={0},total_mp1_cs[6]={0},total_mp2_cs[6]={0};
    double total_accum_mp12[6]={0.0},total_accum_mp21[6]={0.0};
    int total_n_accum[6]={0};
    uint8_t last_code1[6]={0},last_code2[6]={0};
    const char *sys_names[]={"GPS","GLO","GAL","BDS","QZS","SBS"};
    int sys_map[]={SYS_GPS,SYS_GLO,SYS_GAL,SYS_CMP,SYS_QZS,SYS_SBS};
    
    char out_buf[16384]="";
    char *p=out_buf;
    
    p+=sprintf(p,"\nmultipath estimation (sys,signal1,signal2,prn,MP1[m],MP2[m],# of IOD CS, # of MP1 CS, # of MP2 CS, # of valid obs, o/slips)\n\n");
    
    /* first commit the current active arc for all satellites */
    for (i=0;i<MAXSAT;i++) {
        mp_sat_t *ms=&svr->mp_sat[i];
        if (ms->n>=2) {
            ms->accum_mp12 += (ms->sum2_mp12 - ms->sum_mp12 * ms->sum_mp12 / ms->n);
            ms->accum_mp21 += (ms->sum2_mp21 - ms->sum_mp21 * ms->sum_mp21 / ms->n);
            ms->total_n += ms->n;
            ms->n=0;
        }
    }
    
    for (sys=0;sys<4;sys++) { /* GPS, GLO, GAL, BDS */
        int has_obs=0;
        
        /* collect constellation level stats */
        for (i=0;i<MAXSAT;i++) {
            sat=i+1;
            if (satsys(sat,&prn)!=sys_map[sys]) continue;
            mp_sat_t *ms=&svr->mp_sat[i];
            if (ms->n_valid<=0) continue;
            
            has_obs=1;
            total_n_valid[sys]+=ms->n_valid;
            total_iod_cs[sys]+=ms->iod_cs;
            total_mp1_cs[sys]+=ms->mp1_cs;
            total_mp2_cs[sys]+=ms->mp2_cs;
            total_accum_mp12[sys]+=ms->accum_mp12;
            total_accum_mp21[sys]+=ms->accum_mp21;
            total_n_accum[sys]+=ms->total_n;
            if (ms->code1>0) last_code1[sys]=ms->code1;
            if (ms->code2>0) last_code2[sys]=ms->code2;
        }
        
        if (!has_obs) continue;
        
        /* print constellation average */
        double rms12_const = total_n_accum[sys]>0 ? sqrt(total_accum_mp12[sys]/total_n_accum[sys]) : 0.0;
        double rms21_const = total_n_accum[sys]>0 ? sqrt(total_accum_mp21[sys]/total_n_accum[sys]) : 0.0;
        int total_cs = total_iod_cs[sys]+total_mp1_cs[sys]+total_mp2_cs[sys];
        double o_slips_const = total_cs>0 ? (double)total_n_valid[sys]/total_cs : (double)total_n_valid[sys];
        
        char sig1_str[8]="",sig2_str[8]="";
        strcpy(sig1_str,code2obs(last_code1[sys]));
        strcpy(sig2_str,code2obs(last_code2[sys]));
        
        p+=sprintf(p,"%-3s %-2s %-2s    :  %8.6f  %8.6f      %d       %d       %d   %5d   %5.0f\n",
                   sys_names[sys],sig1_str,sig2_str,rms12_const,rms21_const,
                   total_iod_cs[sys],total_mp1_cs[sys],total_mp2_cs[sys],
                   total_n_valid[sys],o_slips_const);
                   
        /* print individual satellite stats */
        for (i=0;i<MAXSAT;i++) {
            sat=i+1;
            if (satsys(sat,&prn)!=sys_map[sys]) continue;
            mp_sat_t *ms=&svr->mp_sat[i];
            if (ms->n_valid<=0) continue;
            
            double rms12 = ms->total_n>0 ? sqrt(ms->accum_mp12/ms->total_n) : 0.0;
            double rms21 = ms->total_n>0 ? sqrt(ms->accum_mp21/ms->total_n) : 0.0;
            int sat_cs = ms->iod_cs+ms->mp1_cs+ms->mp2_cs;
            double o_slips = sat_cs>0 ? (double)ms->n_valid/sat_cs : (double)ms->n_valid;
            
            strcpy(sig1_str,code2obs(ms->code1));
            strcpy(sig2_str,code2obs(ms->code2));
            
            p+=sprintf(p,"%-3s %-2s %-2s %03d:  %8.6f  %8.6f      %d       %d       %d   %5d   %5.0f\n",
                       sys_names[sys],sig1_str,sig2_str,prn,rms12,rms21,
                       ms->iod_cs,ms->mp1_cs,ms->mp2_cs,
                       ms->n_valid,o_slips);
        }
        p+=sprintf(p,"\n");
    }
    
    if (svr->mp==1) {
        printf("%s",out_buf);
        fflush(stdout);
    }
    else if (svr->mp==2) {
        fprintf(stderr,"%s",out_buf);
    }
}

