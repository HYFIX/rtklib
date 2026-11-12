/*------------------------------------------------------------------------------
* rtklib unit test driver : rtcm3 GLONASS ephemeris (1020) date resolution
*
* 1020 carries only the time of day. The date must follow the stream (GPS
* ephemeris epochs) and not the approximated time given by the user.
*-----------------------------------------------------------------------------*/
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "../../src/rtklib.h"

#define DATAFILE "../data/rtcm3/bkg_gps_glo_eph.rtcm3" /* BKG 1019+1020, 2026/10/20 16:00 */

/* decode file with given approximated time, return number of GLONASS eph and
   verify all of them are within [tmin,tmax] ---------------------------------*/
static int run(const double *tr, gtime_t tmin, gtime_t tmax)
{
    rtcm_t rtcm={0};
    FILE *fp;
    int i,data,n=0;
    char s[64];
    
    assert(init_rtcm(&rtcm));
    rtcm.time=epoch2time(tr);
    assert((fp=fopen(DATAFILE,"rb")));
    while ((data=fgetc(fp))!=EOF) input_rtcm3(&rtcm,(unsigned char)data);
    fclose(fp);
    
    for (i=0;i<MAXPRNGLO;i++) {
        if (rtcm.nav.geph[i].toe.time==0) continue;
        time2str(rtcm.nav.geph[i].toe,s,0);
        if (timediff(rtcm.nav.geph[i].toe,tmin)<0.0||
            timediff(rtcm.nav.geph[i].toe,tmax)>0.0||
            timediff(rtcm.nav.geph[i].tof,tmin)<0.0||
            timediff(rtcm.nav.geph[i].tof,tmax)>0.0) {
            fprintf(stderr,"R%02d toe=%s out of range (tr=%.0f:%.0f)\n",i+1,s,tr[3],tr[4]);
            free_rtcm(&rtcm);
            return -1;
        }
        n++;
    }
    free_rtcm(&rtcm);
    return n;
}
/* 1020 date resolution is independent of approximated time -------------------*/
static void test1020date(void)
{
    double tmin[6]={2026,10,20,15, 0,0},tmax[6]={2026,10,20,17,0,0};
    double tr0[6]={2026,10,20, 0, 0,0};  /* midnight: previous-day bug */
    double tr1[6]={2026,10,20,18, 0,0};  /* near the real time */
    double tr2[6]={2026,10,20,23,59,59};
    double tr3[6]={2026,10,21, 8, 0,0};  /* ahead of the data */
    int n0,n1,n2,n3;
    
    n1=run(tr1,epoch2time(tmin),epoch2time(tmax)); assert(n1>=20);
    n0=run(tr0,epoch2time(tmin),epoch2time(tmax)); assert(n0==n1);
    n2=run(tr2,epoch2time(tmin),epoch2time(tmax)); assert(n2==n1);
    n3=run(tr3,epoch2time(tmin),epoch2time(tmax)); assert(n3==n1);
    printf("%s utest1 : OK\n",__FILE__);
}
int main(void)
{
    test1020date();
    return 0;
}
