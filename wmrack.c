/*
 * $Id: wmrack.c,v 1.8 1997/06/14 13:24:57 ograf Exp $
 *
 * WMRack - WindowMaker Sound Control Panel
 *
 * Rewritten by         Oliver Graf  <ograf@fga.de>   http://www.fga.de/~ograf/
 * Graphics designed by Heiko Wagner <hwagner@fga.de> http://www.fga.de/~hwagner/
 *
 * ascd originally By Rob Malda <malda@cs.hope.edu>   http://www.cs.hope.edu/~malda/
 *
 * This is an 'WindowMaker Look & Feel' Dock applet that can be
 * used to control an cdrom. Also works with AfterSteps Wharf.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <X11/extensions/shape.h>

#include <errno.h>
#include <unistd.h>
#include <sys/time.h>

#include <signal.h>

#include "xpmicon.h"
#include "cdrom.h"

/* Functions *****************************************************************/
void  usage();
void  parseCmdLine(int argc, char *argv[]);
void  initHandler();
void  createWindow(int, char **);
void  shutDown(int);
void  mainLoop();
int   flushExpose(Window w);
void  redrawWindow();
void  redrawDisplay(int force_win);
Pixel getColor(char *name);
Time  getTime();

/* Global stuff **************************************************************/
#define PLAY    0
#define PAUSE   1
#define STOP    2
#define UPTRACK 3
#define DNTRACK 4
#define EJECT   5

Display *Disp;
Window Root;
Window Iconwin;
Window Win;
char *Geometry=0;
GC WinGC;

/* varibles for the options */
char *StyleXpmFile=NULL;
char *LedColor=NULL;
char CdDevice[1024]="/dev/cdrom";
int  withdraw=0;
int noprobe=0;

MSF last_time={-1,-1,-1};
int last_track=-1;
int last_cdmode=-1; /* to sense a change */
int displaymode=0;  /* bit 1 is run/remain, bit 2 is track/total */
int playmode=0;     /* 0==play through, 1==repeat all, 2==repeat track */
int repeattrack=-1; /* for playmode 2 */
int newdisc=0;
Time press_time=-1;

/* our cd device */
CD *cd;

/*
 * start the whole stuff and enter the MainLoop
 */
int main(int argc,char *argv[])
{
  parseCmdLine(argc, argv);
  cd=cd_open(CdDevice,noprobe);
  if (cd_getStatus(cd,0)>0)
    newdisc=1;
  initHandler();
  createWindow(argc, argv);
  mainLoop();
  return 0;
}

/*
 * usage()
 *
 * print out usage text and exit
 */
void usage()
{
  fprintf(stderr,"wmrack - Version 0.9\n");
  fprintf(stderr,"usage: wmrack [OPTIONS] \n");
  fprintf(stderr,"\n");
  fprintf(stderr,"OPTION                  DEFAULT        DESCRIPTION\n");
  fprintf(stderr,"-d|--device DEV         /dev/cdrom     device of the Drive\n");
  fprintf(stderr,"-h|--help               none           display help\n");
  fprintf(stderr,"-l|--ledcolor COLSPEC   green          set the color of the led\n");
  fprintf(stderr,"-p|--noprobe            off            disable the startup probe\n");
  fprintf(stderr,"-s|--style STYLEFILE    compile-time   load an alternate set of xpm\n");
  fprintf(stderr,"-w|--withdrawn          off            start withdrawn or not\n");
  fprintf(stderr,"\n");
  exit(1);
}

/*
 * parseCmdLine(argc,argv)
 *
 * parse the command line args
 */
void parseCmdLine(int argc, char *argv[])
{
  int i, j;
  char opt;
  struct {char *name, option;} Options[]={{"device",'d'},
					  {"withdrawn",'w'},
					  {"help",'h'},
					  {"ledcolor",'l'},
					  {"style",'s'},
					  {"noprobe",'p'},
					  {NULL,0}};

  for(i=1; i<argc; i++)
    {
      if (argv[i][0]=='-')
	{
	  if (argv[i][1]=='-')
	    {
	      for (j=0; Options[j].name!=NULL; j++)
		if (strcmp(Options[j].name,argv[i]+2)==0)
		  {
		    opt=Options[j].option;
		    break;
		  }
	      if (Options[j].name==NULL)
		usage();
	    }
	  else
	    {
	      if (argv[i][2]!=0)
		usage();
	      opt=argv[i][1];
	    }
	  switch (opt)
	    {
	    case 'd':  /* Device */
	      if (++i>=argc)
		usage();
	      strcpy(CdDevice,argv[i]);
	      continue;
	    case 'w':  /* start Withdrawn */
	      withdraw=1;
	      continue;
	    case 'h':  /* usage */
	      usage();
	      break;
	    case 'l':  /* Led Color */
	      if (++i>=argc)
		usage();
	      LedColor=strdup(argv[i]);
	      continue;
	    case 's':
	      if (++i>=argc)
		usage();
	      StyleXpmFile=strdup(argv[i]);
	      continue;
	    case 'p':
	      noprobe=1;
	      continue;
	    default:
	      usage();
	    }
	}
      else
	usage();
    }

}

/*
 * initHandler()
 *
 * inits the signal handlers
 */
void initHandler()
{
  struct sigaction sa;
  sa.sa_handler=shutDown;
  sigfillset(&sa.sa_mask);
  sa.sa_flags=0;
  sa.sa_restorer=NULL;
  sigaction(SIGTERM,&sa,NULL);
  sigaction(SIGINT,&sa,NULL);
  sigaction(SIGHUP,&sa,NULL);
  sigaction(SIGQUIT,&sa,NULL);
  sigaction(SIGPIPE,&sa,NULL);
  sa.sa_handler=SIG_IGN;
  sigaction(SIGUSR1,&sa,NULL);
  sigaction(SIGUSR2,&sa,NULL);
}

/*
 * createWindow(argc,argv)
 *
 * create the basic shaped window and set all the required stuff
 */
void createWindow(int argc, char **argv)
{
  int i;
  unsigned int borderwidth ;
  char *display_name=NULL;
  char *wname="wmrack";
  XGCValues gcv;
  unsigned long gcm;
  XTextProperty name;
  Pixel back_pix, fore_pix;
  int screen;
  int x_fd;
  int d_depth;
  int ScreenWidth, ScreenHeight;
  XSizeHints SizeHints;
  XWMHints WmHints;
  XClassHint classHint;

  /* Open display */
  if (!(Disp = XOpenDisplay(display_name)))
    {
      fprintf(stderr,"wmrack: can't open display %s\n", XDisplayName(display_name));
      exit (1);
    }

  screen=DefaultScreen(Disp);
  Root=RootWindow(Disp, screen);
  d_depth=DefaultDepth(Disp, screen);
  x_fd=XConnectionNumber(Disp);
  ScreenHeight=DisplayHeight(Disp,screen);
  ScreenWidth=DisplayWidth(Disp,screen);

  xpm_setDefaultAttr(Disp,Root,LedColor);
  if (StyleXpmFile)
    {
      if (xpm_loadSet(Disp,Root,StyleXpmFile))
	{
	  fprintf(stderr,"wmrack: can't load and create pixmaps\n");
	  XCloseDisplay(Disp);
	  exit(1);
	}
    }
  else if (xpm_setDefaultSet(Disp,Root,RACK_MAX))
    {
      fprintf(stderr,"wmrack: can't create pixmaps\n");
      XCloseDisplay(Disp);
      exit(1);
    }

  SizeHints.flags=USSize|USPosition;
  SizeHints.x=0;
  SizeHints.y=0;
  back_pix=getColor("white");
  fore_pix=getColor("black");

  XWMGeometry(Disp, screen, Geometry, NULL, (borderwidth=1), &SizeHints,
	      &SizeHints.x,&SizeHints.y,&SizeHints.width,
	      &SizeHints.height, &i);

  SizeHints.width=currentXpm(attributes).width;
  SizeHints.height=currentXpm(attributes).height;
  Win=XCreateSimpleWindow(Disp,Root,SizeHints.x,SizeHints.y,
			    SizeHints.width,SizeHints.height,
			    borderwidth,fore_pix,back_pix);
  Iconwin=XCreateSimpleWindow(Disp,Win,SizeHints.x,SizeHints.y,
				SizeHints.width,SizeHints.height,
				borderwidth,fore_pix,back_pix);
  XSetWMNormalHints(Disp, Win, &SizeHints);

  classHint.res_name="wmrack";
  classHint.res_class="WMRack";
  XSetClassHint(Disp, Win, &classHint);

  XSelectInput(Disp, Win, (ExposureMask|ButtonPressMask|ButtonReleaseMask|
			   StructureNotifyMask));
  XSelectInput(Disp, Iconwin, (ExposureMask|ButtonPressMask|ButtonReleaseMask|
			       StructureNotifyMask));

  if (XStringListToTextProperty(&wname, 1, &name) ==0)
    {
      fprintf(stderr, "wmrack: can't allocate window name\n");
      exit(-1);
    }
  XSetWMName(Disp, Win, &name);

  /* Create WinGC */
  gcm=GCForeground|GCBackground|GCGraphicsExposures;
  gcv.foreground=fore_pix;
  gcv.background=back_pix;
  gcv.graphics_exposures=False;
  WinGC=XCreateGC(Disp, Root, gcm, &gcv);

  XShapeCombineMask(Disp, Win, ShapeBounding, 0, 0,
		    currentXpm(mask), ShapeSet);
  XShapeCombineMask(Disp, Iconwin, ShapeBounding, 0, 0,
		    currentXpm(mask), ShapeSet);

  WmHints.initial_state=withdraw?WithdrawnState:NormalState;
  WmHints.icon_window=Iconwin;
  WmHints.window_group=Win;
  WmHints.icon_x=SizeHints.x;
  WmHints.icon_y=SizeHints.y;
  WmHints.flags=StateHint|IconWindowHint|IconPositionHint|WindowGroupHint;
  XSetCommand(Disp, Win, argv, argc);
  XSetWMHints(Disp, Win, &WmHints);
  XMapWindow(Disp,Win);
  redrawDisplay(1);
}

/*
 * shutDown(int sig)
 *
 * handler for signal and close-down function
 */
void shutDown(int sig)
{
#ifdef DEBUG
  fprintf(stderr,"wmrack: Shutting down\n");
#endif
#ifdef 0 /* this is not good, because CloseDisplay woes with this stuff freed */
  xpm_freeSet(Disp);
#  ifdef DEBUG
  fprintf(stderr,"wmrack: XPMs freed\n");
#  endif
  XFreeGC(Disp, WinGC);
#  ifdef DEBUG
  fprintf(stderr,"wmrack: GC freed\n");
#  endif
  XDestroyWindow(Disp, Win);
#  ifdef DEBUG
  fprintf(stderr,"wmrack: Win destroyed\n");
#  endif
  XDestroyWindow(Disp, Iconwin);
#  ifdef DEBUG
  fprintf(stderr,"wmrack: Iconwin destroyed\n");
#  endif
#endif
  XCloseDisplay(Disp);
#ifdef DEBUG
  fprintf(stderr,"wmrack: Display closed\n");
#endif
  exit(0);
}

/*
 * mainLoop()
 *
 * the main event loop
 */
void mainLoop()
{
  XEvent Event;
  Time when;
  int skip_count, skip_amount, skip_delay;

  while(1)
    {
      /* Check events */
      while (XPending(Disp))
	{
	  XNextEvent(Disp,&Event);
	  switch (Event.type)
	    {
	    case Expose:
	      if (Event.xexpose.count==0)
		last_time.minute=last_time.second=last_time.frame=-1;
	      redrawDisplay(1);
	      continue;
	    case ButtonPress:
	      newdisc=0;
	      cd_getStatus(cd,0);
	      if (Event.xbutton.y<15 && curMode(cd)==CDM_PLAY)
		{
		  switch (Event.xbutton.button)
		    {
		    case 1:
		      displaymode^=1;
		      break;
		    case 2:
		      displaymode^=2;
		      break;
		    case 3:
		      playmode++;
		      if (playmode>2)
			playmode=0;
		      if (playmode==2)
			repeattrack=cd->info->current.track;
		      else
			repeattrack=-1;
		      break;
		    }
		}
	      else if (Event.xbutton.y>15 && Event.xbutton.y<32)
		{
		  if (Event.xbutton.x<17)
		    {
		      if (curMode(cd)==CDM_PLAY || curMode(cd)==CDM_PAUSE)
			cd_doPause(cd);
		      else if (curMode(cd)==CDM_EJECT)
			{
			  if (cd_getStatus(cd,1))
			    {
			      newdisc=1;
			      cd_doPlay(cd,cd->info->play.start_track,99);
			    }
			}
		      else
			cd_doPlay(cd,cd->info->play.start_track,99);
		    }
		  else if (Event.xbutton.x>34)
		    {
		      if (curMode(cd)==CDM_PLAY || curMode(cd)==CDM_PAUSE)
			cd_doStop(cd);
		      else if (curMode(cd)==CDM_EJECT)
			{
			  if (cd_getStatus(cd,1))
			    newdisc=1;
			}
		      else
			cd_doEject(cd);
		    }
		}
	      else if (Event.xbutton.y>32)
		{
		  press_time=Event.xbutton.time;
		  skip_count=0;
		  skip_delay=8;
		  if (Event.xbutton.x<17)
		    skip_amount=-1;
		  else if (Event.xbutton.x>34)
		    skip_amount=1;
		}
	      break;
	    case ButtonRelease:
	      if (Event.xbutton.time-press_time<200 && Event.xbutton.y>32)
		{
		  if (curMode(cd)==CDM_PLAY || curMode(cd)==CDM_PAUSE)
		    {
		      if (Event.xbutton.x<17)
			{
			  if (cd->info->current.relmsf.minute ||
			      cd->info->current.relmsf.second>2)
			    cd_doPlay(cd,cd->info->current.track,99);
			  else
			    cd_doPlay(cd,cd->info->current.track-1,99);
			}
		      else if (Event.xbutton.x>34)
			cd_doPlay(cd,cd->info->current.track+1,99);
		    }
		  else if (curMode(cd)==CDM_STOP)
		    {
		      if (Event.xbutton.x<17)
			{
			  if (cd->info->play.start_track>1)
			    cd->info->play.start_track--;
			  else
			    cd->info->play.start_track=cd->info->tracks;
			  if (repeattrack>-1)
			    repeattrack=cd->info->play.start_track;
			}
		      else if (Event.xbutton.x>34)
			{
			  if (cd->info->play.start_track<cd->info->tracks)
			    cd->info->play.start_track++;
			  else
			    cd->info->play.start_track=1;
			  if (repeattrack>-1)
			    repeattrack=cd->info->play.start_track;
			}
		    }
		  else
		    ;
		}
	      press_time=-1;
	      break;
	    case DestroyNotify:
	      shutDown(SIGTERM);
	      break;
	    }
	  XFlush(Disp);
	}
      /* now check for a pressed button */
      if (press_time!=-1)
	{
	  if (curMode(cd)==CDM_PLAY)
	    {
	      when=getTime();
	      if (when-press_time>500)
		{
		  skip_count++;
		  if (skip_count%skip_delay==0)
		    {
		      if (Event.xbutton.x<17)
			cd_doSkip(cd,skip_amount);
		      else if (Event.xbutton.x>34)
			cd_doSkip(cd,skip_amount);
		    }
		  switch (skip_count)
		    {
		    case 10:
		    case 25:
		    case 50:
		      skip_delay>>=1;
		      break;
		    }
		}
	    }
	}
      /* do a redraw of the LED display */
      redrawDisplay(0);
      usleep(1000L);
    }
}

/*
 * flushExpose(window)
 *
 * remove any Expose events from the current EventQueue
 */
int flushExpose(Window w)
{
  XEvent dummy;
  int i=0;

  while (XCheckTypedWindowEvent (Disp, w, Expose, &dummy)) i++;
  return i;
}

/*
 * redrawWindow()
 *
 * combine mask and draw pixmap
 */
void redrawWindow()
{
  flushExpose(Win);
  flushExpose(Iconwin);
  
  XShapeCombineMask(Disp, Win, ShapeBounding, 0, 0,
		    currentXpm(mask), ShapeSet);
  XShapeCombineMask(Disp, Iconwin, ShapeBounding, 0, 0,
		    currentXpm(mask), ShapeSet);
  
  XCopyArea(Disp,currentXpm(pixmap),Win,WinGC,
	    0,0,currentXpm(attributes).width, currentXpm(attributes).height,0,0);
  XCopyArea(Disp,currentXpm(pixmap),Iconwin,WinGC,
	    0,0,currentXpm(attributes).width, currentXpm(attributes).height,0,0);
}

/*
 * split this function into the real redraw and a pure 
 * display time/track function.
 * redraw wants a complete redraw (covering, movement, etc.)
 * but the display of the time/track does not need this overhead (SHAPE)
 */
void redrawDisplay(int force_win)
{
  int track[2]={0,0};
  int cdtime[4]={0,0,0,0};
  static time_t last_flash_time;
  static flash=0;
  int st=0, newRack=RACK_NODISC, im_stop=0;
  MSF pos;

  st=cd_getStatus(cd,0);

  if (st>0 && cd->info->current.track>cd->info->tracks &&
      curMode(cd)!=CDM_EJECT)
    {
      cd_doStop(cd);
      im_stop=1;
    }
  else
    if (!force_win && 
	curMode(cd)!=CDM_PAUSE &&
	last_cdmode==curMode(cd) &&
	(st<1 ||
	 (last_time.minute==cd->info->current.relmsf.minute &&
	  last_time.second==cd->info->current.relmsf.second &&
	  last_track==cd->info->play.start_track)))
      /* no change in second or mode, and no pause blink */
      return;
  
#ifdef DEBUG
  if (last_cdmode!=curMode(cd)) {
    fprintf(stderr,"wmrack: cur_cdmode %d\n",curMode(cd));
  }
#endif

  switch (playmode) /* handle repeats */
    {
    case 1:
      if (curMode(cd)==CDM_COMP || im_stop)
	cd_doPlay(cd,1,99);
      break;
    case 2:
      if ((repeattrack>-1 &&
	   cd->info->current.track!=repeattrack &&
	   curMode(cd)==CDM_PLAY) ||
	  (repeattrack>-1 &&
	   (curMode(cd)==CDM_COMP || im_stop)))
	cd_doPlay(cd,repeattrack,repeattrack);
      break;
    }
  
  last_cdmode=curMode(cd);
  if (st>0)
    {
      last_time=cd->info->current.relmsf;
      last_track=cd->info->play.start_track;
    }
  else
    {
      MSFnone(last_time);
      last_track=-1;
    }
  
  if (curMode(cd)==CDM_PAUSE)
    {
      time_t flash_time=time(NULL);
      if (flash_time==last_flash_time && !force_win)
	return;
      last_flash_time=flash_time;
      flash=!flash;
    }
  else
    {
      last_flash_time=0;
      flash=1;
    }
  
  newRack=RACK_PLAY;
  switch (curMode(cd))
    {
    case CDM_PAUSE:
      newRack=RACK_PAUSE;
    case CDM_PLAY:
      track[0]=cd->info->current.track/10;
      track[1]=cd->info->current.track%10;
      switch (displaymode)
	{
	case 0:
	  pos=cd->info->current.relmsf;
	  break;
	case 1:
	  pos=subMSF(cd->info->track[cd->info->current.track].toc,
		     cd->info->current.absmsf);
	  break;
	case 2:   /* this does not work */
	  pos=subMSF(cd->info->current.absmsf,
		     cd->info->track[0].toc);
	  break;
	case 3:
	  pos=subMSF(cd->info->track[cd->info->tracks].toc,
		     cd->info->current.absmsf);
	  break;
	}
      cdtime[0]=pos.minute/10;
      cdtime[1]=pos.minute%10;
      cdtime[2]=pos.second/10;
      cdtime[3]=pos.second%10;
      break;
    case CDM_STOP:
      newRack=RACK_STOP;
      if (newdisc)
	{
	  track[0]=cd->info->tracks/10;
	  track[1]=cd->info->tracks%10;
	}
      else
	{
	  track[0]=cd->info->play.start_track/10;
	  track[1]=cd->info->play.start_track%10;
	}
      cdtime[0]=(cd->info->slen/60)/10;
      cdtime[1]=(cd->info->slen/60)%10;
      cdtime[2]=(cd->info->slen%60)/10;
      cdtime[3]=(cd->info->slen%10);
      break;
    case CDM_COMP:
      newRack=RACK_STOP;
      goto set_null;
    case CDM_EJECT:
      newRack=RACK_NODISC;
    default:
    set_null:
      track[0]= 0;
      track[1]= 0;
      cdtime[0]=0;
      cdtime[1]=0;
      cdtime[2]=0;
      cdtime[3]=0;
      break;
    }

  if (newRack!=curRack || force_win)
    {
      /* Mode has changed, paint new mask and pixmap */
      curRack=newRack;
      redrawWindow();
    }

  XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
	    (track[0]?8*track[0]:80),0, 8,11, 16,35);
  XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
	    (track[0]?8*track[0]:80),0, 8,11, 16,35);
  XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
	    8*track[1],0, 8,11, 24,35);
  XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
	    8*track[1],0, 8,11, 24,35);

  if (flash || curMode(cd)!=CDM_PAUSE)
    {
      if (curMode(cd)==CDM_PLAY || curMode(cd)==CDM_PAUSE)
	{
	  XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		    ((displaymode&2)?94:98),0, 4,5, 3,2);
	  XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		    ((displaymode&2)?94:98),0, 4,5, 3,2);
	  XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		    ((displaymode&1)?94:98),5, 4,6, 3,7);
	  XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		    ((displaymode&1)?94:98),5, 4,6, 3,7);
	}
      else
	{
	  XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		    98,0, 4,11, 3,2);
	  XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		    98,0, 4,11, 3,2);
	}

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		(cdtime[0]?8*cdtime[0]:80),0, 8,11, 7,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		(cdtime[0]?8*cdtime[0]:80),0, 8,11, 7,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		8*cdtime[1],0, 8,11, 15,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		8*cdtime[1],0, 8,11, 15,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		88,0, 3,11, 23,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		88,0, 3,11, 23,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		8*cdtime[2],0, 8,11, 26,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		8*cdtime[2],0, 8,11, 26,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		8*cdtime[3],0, 8,11, 34,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		8*cdtime[3],0, 8,11, 34,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		(playmode?102:106),0, 4,5, 42,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		(playmode?102:106),0, 4,5, 42,2);
      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		(playmode==2?102:106),6, 4,5, 42,8);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		(playmode==2?102:106),6, 4,5, 42,8);
    }
  else
    {
      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		98,0, 4,11, 3,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		98,0, 4,11, 3,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		80,0, 8,11, 7,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		80,0, 8,11, 7,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		80,0, 8,11, 15,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		80,0, 8,11, 15,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		91,0, 3,11, 23,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		91,0, 3,11, 23,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		80,0, 8,11, 26,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		80,0, 8,11, 26,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		80,0, 8,11, 34,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		80,0, 8,11, 34,2);

      XCopyArea(Disp, currentLed(pixmap), Win, WinGC,
		106,0, 4,11, 42,2);
      XCopyArea(Disp, currentLed(pixmap), Iconwin, WinGC,
		106,0, 4,11, 42,2);
    }

}

/*
 * getColor(colorname)
 *
 * save way to get a color from X
 */
Pixel getColor(char *ColorName)
{
  XColor Color;
  XWindowAttributes Attributes;

  XGetWindowAttributes(Disp,Root,&Attributes);
  Color.pixel = 0;

  if (!XParseColor (Disp, Attributes.colormap, ColorName, &Color))
    fprintf(stderr,"wmrack: can't parse %s\n", ColorName);
  else if(!XAllocColor (Disp, Attributes.colormap, &Color))
    fprintf(stderr,"wmrack: can't allocate %s\n", ColorName);

  return Color.pixel;
}

/*
 * getTime()
 *
 * returns the time in milliseconds
 */
Time getTime()
{
  struct timeval tv;
  gettimeofday(&tv,NULL);
  return (tv.tv_sec*1000)+(tv.tv_usec/1000);
}
