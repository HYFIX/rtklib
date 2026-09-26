/*-----------------------------------------------------------------------------
* geod2rtkp.c : stream GEOD-wrapped RTCM logs into the RTK positioning engine
*
* The input files are merged by the UTC millisecond value in each
* $GEOD,<utc_ms>,<length>,<payload> record. Only one record per file and one
* rover/base observation epoch are retained, so memory use is independent of
* log duration.
*-----------------------------------------------------------------------------*/
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtklib.h"

#define PROGNAME "geod2rtkp"
#define MAXINFILE 16
#define MAXGEODLEN (64U*1024U*1024U)

typedef struct {
    FILE *fp;
    const char *path;
    rtcm_t rtcm;
    uint8_t *payload;
    size_t capacity;
    size_t length;
    int64_t utc_ms;
    int role; /* 0: rover, 1: base, 2: navigation/correction */
    int ready;
    uint64_t records;
    uint64_t bytes;
    uint64_t malformed;
} geod_stream_t;

typedef struct {
    obsd_t data[MAXOBS];
    int n;
    int valid;
} epoch_t;

static const char *help[]={
"",
" usage: geod2rtkp [option]... rover.log base.log nav.log [nav.log ...]",
"",
" Replay GEOD-wrapped RTCM logs in message-arrival order and compute RTK",
" solutions without loading the complete observation set into memory.",
" The first file is rover, the second is base, and remaining files contain",
" navigation/correction data. Records are merged by their GEOD UTC timestamp.",
"",
" -?        print help",
" -k file   input options from configuration file",
" -o file   set output file [stdout]",
" -p mode   positioning mode [2:kinematic]",
" -f freq   frequency plan (1-6, 7:alternate nf=2 L1+L5)",
" -m mask   elevation mask angle (deg)",
" -sys list navigation systems (G,R,E,J,C,I)",
" -r x y z  reference receiver ECEF position (m)",
" -l lat lon h reference latitude/longitude/height (deg/m)",
" -e        output ECEF position",
" -a        output ENU baseline",
" -g        output latitude/longitude as degrees/minutes/seconds",
" -t        output calendar time",
" -u        output UTC time",
" -d n      decimal places in time",
" -s sep    output field separator",
" -x level  debug trace level"
};

extern int showmsg(const char *format, ...)
{
    va_list arg;
    va_start(arg,format); vfprintf(stderr,format,arg); va_end(arg);
    fprintf(stderr,"\r");
    return 0;
}
extern void settspan(gtime_t ts, gtime_t te) {}
extern void settime(gtime_t time) {}

static void printhelp(void)
{
    size_t i;
    for (i=0;i<sizeof(help)/sizeof(*help);i++) fprintf(stderr,"%s\n",help[i]);
}

/* scan to the next GEOD prefix, allowing recovery after a damaged record */
static int find_prefix(FILE *fp)
{
    static const char prefix[]="$GEOD,";
    int c,state=0;
    while ((c=fgetc(fp))!=EOF) {
        if (c==prefix[state]) {
            if (++state==(int)sizeof(prefix)-1) return 1;
        }
        else state=c==prefix[0]?1:0;
    }
    return 0;
}

static int read_number(FILE *fp, int64_t *value)
{
    int c,ndigit=0;
    int64_t v=0;
    while ((c=fgetc(fp))!=EOF&&c!=',') {
        if (c<'0'||c>'9'||ndigit>=19) return 0;
        v=v*10+(c-'0');
        ndigit++;
    }
    if (c!=','||!ndigit) return 0;
    *value=v;
    return 1;
}

/* return 1: record, 0: eof, -1: unrecoverable allocation/read error */
static int read_record(geod_stream_t *s)
{
    int64_t utc_ms,length;
    int c;
    for (;;) {
        if (!find_prefix(s->fp)) return 0;
        if (!read_number(s->fp,&utc_ms)||!read_number(s->fp,&length)||
            length<0||(uint64_t)length>MAXGEODLEN) {
            s->malformed++;
            continue;
        }
        if ((size_t)length>s->capacity) {
            uint8_t *p=(uint8_t *)realloc(s->payload,(size_t)length);
            if (!p&&length>0) return -1;
            s->payload=p;
            s->capacity=(size_t)length;
        }
        if (length>0&&fread(s->payload,1,(size_t)length,s->fp)!=(size_t)length) {
            return -1;
        }
        /* Writer adds CRLF outside the payload. Tolerate a missing terminator. */
        c=fgetc(s->fp);
        if (c=='\r') {
            c=fgetc(s->fp);
            if (c!=EOF&&c!='\n') ungetc(c,s->fp);
        }
        else if (c!=EOF&&c!='\n') ungetc(c,s->fp);
        s->utc_ms=utc_ms;
        s->length=(size_t)length;
        s->ready=1;
        s->records++;
        s->bytes+=(uint64_t)length;
        return 1;
    }
}

static gtime_t utc_ms_time(int64_t utc_ms)
{
    gtime_t t;
    t.time=(time_t)(utc_ms/1000);
    t.sec=(double)(utc_ms%1000)/1000.0;
    if (t.sec<0.0) {t.time--; t.sec+=1.0;}
    return utc2gpst(t);
}

static int init_nav(nav_t *nav)
{
    memset(nav,0,sizeof(*nav));
    nav->eph =(eph_t  *)calloc(MAXSAT*2,sizeof(eph_t));
    nav->geph=(geph_t *)calloc(NSATGLO,sizeof(geph_t));
    nav->seph=(seph_t *)calloc(NSATSBS,sizeof(seph_t));
    if (!nav->eph||!nav->geph||!nav->seph) return 0;
    nav->n=nav->nmax=MAXSAT*2;
    nav->ng=nav->ngmax=NSATGLO;
    nav->ns=nav->nsmax=NSATSBS;
    return 1;
}

static void free_nav(nav_t *nav)
{
    free(nav->eph); free(nav->geph); free(nav->seph);
    nav->eph=NULL; nav->geph=NULL; nav->seph=NULL;
}

static void update_ephemeris(nav_t *nav, const rtcm_t *rtcm)
{
    int prn,sys=satsys(rtcm->ephsat,&prn);
    if (sys==SYS_GLO) {
        if (prn>=1&&prn<=NSATGLO) {
            nav->geph[prn-1]=rtcm->nav.geph[prn-1];
            nav->glo_fcn[prn-1]=rtcm->nav.geph[prn-1].frq+8;
        }
    }
    else if (rtcm->ephsat>=1&&rtcm->ephsat<=MAXSAT&&rtcm->ephset<2) {
        nav->eph[rtcm->ephsat-1+MAXSAT*rtcm->ephset]=
            rtcm->nav.eph[rtcm->ephsat-1+MAXSAT*rtcm->ephset];
    }
}

static void update_ionutc(nav_t *nav, const nav_t *src)
{
    matcpy(nav->utc_gps,src->utc_gps,8,1);
    matcpy(nav->utc_glo,src->utc_glo,8,1);
    matcpy(nav->utc_gal,src->utc_gal,8,1);
    matcpy(nav->utc_qzs,src->utc_qzs,8,1);
    matcpy(nav->utc_cmp,src->utc_cmp,8,1);
    matcpy(nav->utc_irn,src->utc_irn,9,1);
    matcpy(nav->utc_sbs,src->utc_sbs,4,1);
    matcpy(nav->ion_gps,src->ion_gps,8,1);
    matcpy(nav->ion_gal,src->ion_gal,4,1);
    matcpy(nav->ion_qzs,src->ion_qzs,8,1);
    matcpy(nav->ion_cmp,src->ion_cmp,8,1);
    matcpy(nav->ion_irn,src->ion_irn,8,1);
}

static void update_ssr(nav_t *nav, rtcm_t *rtcm)
{
    int i;
    for (i=0;i<MAXSAT;i++) {
        if (!rtcm->ssr[i].update) continue;
        if (rtcm->ssr[i].iod[0]==rtcm->ssr[i].iod[1]) nav->ssr[i]=rtcm->ssr[i];
        rtcm->ssr[i].update=0;
    }
}

static void save_epoch(epoch_t *dst, const obs_t *src, int rcv,
                       const prcopt_t *opt)
{
    int i,sat,sys;
    dst->n=0;
    for (i=0;i<src->n&&dst->n<MAXOBS;i++) {
        sat=src->data[i].sat;
        sys=satsys(sat,NULL);
        if (sat<=0||sat>MAXSAT||opt->exsats[sat-1]==1||!(sys&opt->navsys)) continue;
        dst->data[dst->n]=src->data[i];
        dst->data[dst->n++].rcv=(uint8_t)rcv;
    }
    dst->valid=dst->n>0;
}

static int same_epoch(const epoch_t *a, const epoch_t *b)
{
    return a->valid&&b->valid&&fabs(timediff(a->data[0].time,b->data[0].time))<=DTTOL;
}

static int process_epoch(rtk_t *rtk, const epoch_t *rover, const epoch_t *base,
                         nav_t *nav, FILE *fp, const solopt_t *sopt)
{
    obsd_t obs[MAXOBS*2];
    int i,n=0;
    if (!rover->valid) return 0;
    for (i=0;i<rover->n&&n<MAXOBS*2;i++) obs[n++]=rover->data[i];
    if (base->valid) for (i=0;i<base->n&&n<MAXOBS*2;i++) obs[n++]=base->data[i];
    rtkpos(rtk,obs,n,nav);
    if (rtk->sol.stat==SOLQ_NONE) return 0;
    outsol(fp,&rtk->sol,rtk->rb,sopt);
    return 1;
}

static void apply_message(geod_stream_t *s, int ret, nav_t *nav,
                          prcopt_t *opt, epoch_t *rover, epoch_t *base,
                          rtk_t *rtk, FILE *out, const solopt_t *sopt,
                          uint64_t *solutions)
{
    int i;
    if (ret==1) {
        if (s->role==0) {
            /* A single pending rover epoch permits a matching base message to
             * arrive next without accumulating observations over log time. */
            if (rover->valid) {
                *solutions+=process_epoch(rtk,rover,base,nav,out,sopt);
            }
            save_epoch(rover,&s->rtcm.obs,1,opt);
            if (same_epoch(rover,base)) {
                *solutions+=process_epoch(rtk,rover,base,nav,out,sopt);
                rover->valid=0;
            }
        }
        else if (s->role==1) {
            save_epoch(base,&s->rtcm.obs,2,opt);
            if (same_epoch(rover,base)) {
                *solutions+=process_epoch(rtk,rover,base,nav,out,sopt);
                rover->valid=0;
            }
        }
    }
    else if (ret==2) update_ephemeris(nav,&s->rtcm);
    else if (ret==5&&s->role==1&&opt->refpos==POSOPT_RTCM) {
        for (i=0;i<3;i++) rtk->rb[i]=s->rtcm.sta.pos[i];
    }
    else if (ret==9) update_ionutc(nav,&s->rtcm.nav);
    else if (ret==10) update_ssr(nav,&s->rtcm);
}

static int select_next(geod_stream_t *streams, int n)
{
    int i,best=-1;
    for (i=0;i<n;i++) if (streams[i].ready&&
        (best<0||streams[i].utc_ms<streams[best].utc_ms||
         (streams[i].utc_ms==streams[best].utc_ms&&i<best))) best=i;
    return best;
}

int main(int argc, char **argv)
{
    prcopt_t popt=prcopt_default;
    solopt_t sopt=solopt_default;
    filopt_t fopt={{0}};
    geod_stream_t *streams=NULL;
    epoch_t rover={{0}},base={{0}};
    nav_t *nav=NULL;
    rtk_t *rtk=NULL;
    FILE *out=stdout;
    char *files[MAXINFILE],*outfile="",*p;
    double pos[3]={0};
    uint64_t solutions=0,total_records=0,total_bytes=0;
    int i,j,nfile=0,index,ret,status=0,trace_open=0,stat_open=0;

    if (!(streams=(geod_stream_t *)calloc(MAXINFILE,sizeof(*streams)))||
        !(nav=(nav_t *)calloc(1,sizeof(*nav)))||
        !(rtk=(rtk_t *)calloc(1,sizeof(*rtk)))) {
        fprintf(stderr,"stream allocation error\n");
        free(streams); free(nav); free(rtk);
        return -1;
    }

    popt.mode=PMODE_KINEMA;
    popt.navsys=0;
    popt.refpos=POSOPT_SINGLE;
    popt.glomodear=1;
    sopt.timef=1;
    snprintf(sopt.prog,sizeof(sopt.prog),"%s ver.%s %s",PROGNAME,VER_RTKLIB,PATCH_LEVEL);
    snprintf(fopt.trace,sizeof(fopt.trace),"%s.trace",PROGNAME);

    for (i=1;i<argc;i++) if (!strcmp(argv[i],"-k")&&i+1<argc) {
        resetsysopts();
        if (!loadopts(argv[++i],sysopts)) {free(streams); free(nav); free(rtk); return -1;}
        getsysopts(&popt,&sopt,&fopt);
    }
    for (i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-?")||!strcmp(argv[i],"--help")) {printhelp(); free(streams); free(nav); free(rtk); return 0;}
        else if (!strcmp(argv[i],"-k")&&i+1<argc) i++;
        else if (!strcmp(argv[i],"-o")&&i+1<argc) outfile=argv[++i];
        else if (!strcmp(argv[i],"-p")&&i+1<argc) popt.mode=atoi(argv[++i]);
        else if (!strcmp(argv[i],"-f")&&i+1<argc) {
            popt.nf=atoi(argv[++i]);
            if (popt.nf==7) {popt.nf=2; popt.freqopt=1;}
            else popt.freqopt=0;
        }
        else if (!strcmp(argv[i],"-m")&&i+1<argc) popt.elmin=atof(argv[++i])*D2R;
        else if (!strcmp(argv[i],"-sys")&&i+1<argc) {
            popt.navsys=0;
            for (p=argv[++i];*p;p++) switch (*p) {
                case 'G': popt.navsys|=SYS_GPS; break;
                case 'R': popt.navsys|=SYS_GLO; break;
                case 'E': popt.navsys|=SYS_GAL; break;
                case 'J': popt.navsys|=SYS_QZS; break;
                case 'C': popt.navsys|=SYS_CMP; break;
                case 'I': popt.navsys|=SYS_IRN; break;
            }
        }
        else if (!strcmp(argv[i],"-r")&&i+3<argc) {
            popt.refpos=popt.rovpos=POSOPT_POS;
            for (j=0;j<3;j++) popt.rb[j]=atof(argv[++i]);
            matcpy(popt.ru,popt.rb,3,1);
        }
        else if (!strcmp(argv[i],"-l")&&i+3<argc) {
            popt.refpos=popt.rovpos=POSOPT_POS;
            for (j=0;j<3;j++) pos[j]=atof(argv[++i]);
            pos[0]*=D2R; pos[1]*=D2R; pos2ecef(pos,popt.rb);
            matcpy(popt.ru,popt.rb,3,1);
        }
        else if (!strcmp(argv[i],"-e")) sopt.posf=SOLF_XYZ;
        else if (!strcmp(argv[i],"-a")) sopt.posf=SOLF_ENU;
        else if (!strcmp(argv[i],"-g")) sopt.degf=1;
        else if (!strcmp(argv[i],"-t")) sopt.timef=1;
        else if (!strcmp(argv[i],"-u")) sopt.times=TIMES_UTC;
        else if (!strcmp(argv[i],"-d")&&i+1<argc) sopt.timeu=atoi(argv[++i]);
        else if (!strcmp(argv[i],"-s")&&i+1<argc) strncpy(sopt.sep,argv[++i],sizeof(sopt.sep)-1);
        else if (!strcmp(argv[i],"-x")&&i+1<argc) sopt.trace=atoi(argv[++i]);
        else if (argv[i][0]=='-') {fprintf(stderr,"unknown option: %s\n",argv[i]); free(streams); free(nav); free(rtk); return -1;}
        else if (nfile<MAXINFILE) files[nfile++]=argv[i];
    }
    if (nfile<2) {printhelp(); free(streams); free(nav); free(rtk); return -1;}
    if (!popt.navsys) popt.navsys=SYS_GPS|SYS_GLO;
    if (*outfile&&!(out=fopen(outfile,"wb"))) {
        fprintf(stderr,"output open error: %s\n",outfile); free(streams); free(nav); free(rtk); return -1;
    }
    if (sopt.trace>0) {traceopen(fopt.trace); tracelevel(sopt.trace); trace_open=1;}
    if (sopt.sstat>0&&*fopt.solstat) stat_open=rtkopenstat(fopt.solstat,sopt.sstat);
    if (!init_nav(nav)) {fprintf(stderr,"navigation allocation error\n"); status=-1; goto cleanup;}
    rtkinit(rtk,&popt);
    matcpy(rtk->rb,popt.rb,3,1);
    outsolhead(out,&sopt);

    for (i=0;i<nfile;i++) {
        streams[i].path=files[i];
        streams[i].role=i<2?i:2;
        if (!(streams[i].fp=fopen(files[i],"rb"))) {
            fprintf(stderr,"input open error: %s\n",files[i]); status=-1; goto cleanup_rtk;
        }
        if (!init_rtcm(&streams[i].rtcm)) {
            fprintf(stderr,"RTCM allocation error: %s\n",files[i]); status=-1; goto cleanup_rtk;
        }
        ret=read_record(&streams[i]);
        if (ret<0) {fprintf(stderr,"GEOD read error: %s\n",files[i]); status=-1; goto cleanup_rtk;}
    }
    while ((index=select_next(streams,nfile))>=0) {
        geod_stream_t *s=&streams[index];
        s->rtcm.time=utc_ms_time(s->utc_ms);
        for (i=0;i<(int)s->length;i++) {
            ret=input_rtcm3(&s->rtcm,s->payload[i]);
            if (ret) apply_message(s,ret,nav,&popt,&rover,&base,rtk,out,&sopt,&solutions);
        }
        s->ready=0;
        ret=read_record(s);
        if (ret<0) {fprintf(stderr,"GEOD read error: %s\n",s->path); status=-1; break;}
    }
    if (rover.valid) solutions+=process_epoch(rtk,&rover,&base,nav,out,&sopt);
    for (i=0;i<nfile;i++) {total_records+=streams[i].records; total_bytes+=streams[i].bytes;}
    fprintf(stderr,"%s: %llu records, %llu RTCM bytes, %llu solutions\n",PROGNAME,
            (unsigned long long)total_records,(unsigned long long)total_bytes,
            (unsigned long long)solutions);

cleanup_rtk:
    rtkfree(rtk);
cleanup:
    free_nav(nav);
    for (i=0;i<nfile;i++) {
        if (streams[i].fp) fclose(streams[i].fp);
        if (streams[i].rtcm.obs.data) free_rtcm(&streams[i].rtcm);
        free(streams[i].payload);
    }
    if (stat_open) rtkclosestat();
    if (trace_open) traceclose();
    if (out!=stdout) fclose(out);
    free(streams);
    free(nav);
    free(rtk);
    return status;
}
