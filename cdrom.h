/*
 * $Id: cdrom.h,v 1.3 1997/06/12 14:51:32 ograf Exp $
 */

#ifndef _MY_CDROM_H
#define _MY_CDROM_H

/* cd status modes */
#define CDM_CLOSED -1
#define CDM_COMP    0
#define CDM_PLAY    1
#define CDM_PAUSE   2
#define CDM_STOP    3
#define CDM_EJECT   CDM_CLOSED

typedef struct {
  int minute;
  int second;
  int frame;
} MSF;

#define fSecs(f) (int)(f/75)
#define fMins(f) (int)(f/75/60)

#define msfFrames(msf) (((msf.minute*60)+msf.second)*75+msf.frame)
#define msfSecs(msf)   (((msf.minute*60)+msf.second)

#define MSFnone(msf)   msf.minute=msf.second=msf.frame=-1

typedef struct {
  int num;
  MSF toc;    /* real start */
  int start;  /* start in frames */
  int len;    /* length in frames */
  int slen;   /* length in seconds */
  int data;   /* datatrack? */
} TrackInfo;

typedef struct {
  int mode;
  int track;
  int index;
  MSF relmsf;
  MSF absmsf;
} CDPosition;

typedef struct {    /* stores information about the last play action */
  int start_track;
  int end_track;
} CDPlayInfo;

typedef struct {
  CDPosition    current;
  CDPlayInfo    play;
  unsigned long discid;
  int           len;     /* cd length in frames */
  int           slen;    /* cd length in seconds */
  int           start;   /* first track of the cd */
  int           end;     /* last track of the cd */
  int           tracks;  /* number of tracks (end-start)+1 */
  TrackInfo     *track;
} CDInfo;

typedef struct {
  int    fd;
  char   *device;
  int    status;   /* this keeps the return value of cd_getStatus() */
  int    numcd;
  CDInfo *info;
} CD;

CD *cd_open(char *device, int noopen);
int cd_reopen(CD *cd);
int cd_suspend(CD *cd);
void cd_close(CD *cd);

void cdinfo_free(CDInfo *info);

int cd_getStatus(CD *cd, int reopen);

/*
 * all cd_do* commands assume that prior to their call a cd_getStatus
 * is done to update the CDPosition struct of the cd
 */
int cd_doPlay(CD *cd, int start, int end);
int cd_doSkip(CD *cd, int secs);
int cd_doPause(CD *cd);
int cd_doStop(CD *cd);
int cd_doEject(CD *cd);

int cmpMSF(MSF a, MSF b);
MSF subMSF(MSF a, MSF b);

#define curMode(c) (c->info==NULL?CDM_EJECT:c->info->current.mode)

#endif
