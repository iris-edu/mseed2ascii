/***************************************************************************
 * mseed2ascii.c
 *
 * Convert miniSEED waveform data to ASCII
 *
 * Written by Chad Trabant, IRIS Data Management Center
 *
 * modified 2017.199
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include <libmseed.h>

#define VERSION "2.2"
#define PACKAGE "mseed2ascii"

struct listnode {
  char *key;
  char *data;
  struct listnode *next;
};

static int64_t writeascii (MSTrace *mst);
static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt, int dasharg);
static int readlistfile (char *listfile);
static struct listnode *addnode (struct listnode **listroot, void *key, int keylen,
				 void *data, int datalen);
static void usage (void);

static int    verbose      = 0;    /* Verbosity level */
static int    reclen       = -1;   /* Record length, -1 = autodetected */
static int    deriverate   = 0;    /* Use sample rate derived instead of the reported rate */
static int    indifile     = 0;    /* Individual file processing flag */
static char  *unitsstr     = "Counts"; /* Units to write into output headers */
static char  *outputfile   = 0;    /* Output file name for single file output */
static FILE  *ofp          = 0;    /* Output file pointer */
static int    outformat    = 1;    /* Output file format */
static int    slistcols    = 1;    /* Number of columns for sample list output */
static double timetol      = -1.0; /* Time tolerance for continuous traces */
static double sampratetol  = -1.0; /* Sample rate tolerance for continuous traces */

/* A list of input files */
struct listnode *filelist = 0;


int
main (int argc, char **argv)
{
  MSTraceGroup *mstg = 0;
  MSTrace *mst;
  MSRecord *msr = 0;

  struct listnode *flp;

  int retcode;
  int64_t totalrecs = 0;
  int64_t totalsamps = 0;
  int totalfiles = 0;

  /* Process given parameters (command line and parameter file) */
  if (parameter_proc (argc, argv) < 0)
    return -1;

  /* Init MSTraceGroup */
  mstg = mst_initgroup (mstg);

  /* Open the output file if specified */
  if ( outputfile )
  {
    if ( strcmp (outputfile, "-") == 0 )
    {
      ofp = stdout;
    }
    else if ( (ofp = fopen (outputfile, "wb")) == NULL )
    {
      fprintf (stderr, "Cannot open output file: %s (%s)\n",
               outputfile, strerror(errno));
      return -1;
    }
  }

  /* Read input miniSEED files into MSTraceGroup */
  flp = filelist;
  while ( flp != 0 )
  {
    if ( verbose )
      fprintf (stderr, "Reading %s\n", flp->data);

    while ( (retcode = ms_readmsr(&msr, flp->data, reclen, NULL, NULL,
                                  1, 1, verbose-1)) == MS_NOERROR )
    {
      if ( verbose > 1)
        msr_print (msr, verbose - 2);

      mst_addmsrtogroup (mstg, msr, 1, timetol, sampratetol);

      totalrecs++;
      totalsamps += msr->samplecnt;
    }

    if ( retcode != MS_ENDOFFILE )
      fprintf (stderr, "Error reading %s: %s\n", flp->data, ms_errorstr(retcode));

    /* Make sure everything is cleaned up */
    ms_readmsr (&msr, NULL, 0, NULL, NULL, 0, 0, 0);

    /* If processing each file individually, write ASCII and reset */
    if ( indifile )
    {
      mst = mstg->traces;
      while ( mst )
      {
        writeascii (mst);
        mst = mst->next;
      }

      mstg = mst_initgroup (mstg);
    }

    totalfiles++;
    flp = flp->next;
  }

  if ( ! indifile )
  {
    mst = mstg->traces;
    while ( mst )
    {
      writeascii (mst);
      mst = mst->next;
    }
  }

  /* Make sure everything is cleaned up */
  mst_freegroup (&mstg);

  if ( ofp )
    fclose (ofp);

  if ( verbose )
    printf ("Files: %d, Records: %lld, Samples: %lld\n", totalfiles,
	    (long long int)totalrecs, (long long int)totalsamps);

  return 0;
}  /* End of main() */


/***************************************************************************
 * writeascii:
 *
 * Write data buffer to output file as ASCII.
 *
 * Returns the number of samples written or -1 on error.
 ***************************************************************************/
static int64_t
writeascii (MSTrace *mst)
{
  BTime btime;
  char outfile[1024];
  char *outname = outputfile;
  char timestr[50];
  char srcname[50];
  char *samptype;

  int month, mday;
  int col, cnt, samplesize;
  int64_t line;
  int64_t lines;
  void *sptr;

  if ( ! mst )
    return -1;

  if ( mst->numsamples == 0 || mst->samprate == 0.0 )
    return 0;

  /* Check reported versus derived sampling rates */
  if ( mst->starttime < mst->endtime )
  {
    hptime_t hptimeshift;
    hptime_t hpdelta;
    double samprate;

    /* Calculate difference between end time of last miniSEED record and the end time
     * as calculated based on the start time, reported sample rate and number of samples. */
    hptimeshift = llabs (mst->endtime - mst->starttime - (hptime_t)((mst->numsamples - 1) * HPTMODULUS / mst->samprate));

    /* Calculate high-precision sample period using reported sample rate */
    hpdelta = (hptime_t)(( mst->samprate ) ? (HPTMODULUS / mst->samprate) : 0.0);

    /* Test if time shift is beyond half a sample period */
    if ( hptimeshift > (hpdelta * 0.5) )
    {
      /* Derive sample rate from start and end times and number of samples */
      samprate = (double) (mst->numsamples - 1) * HPTMODULUS / (mst->endtime - mst->starttime);

      if ( deriverate )
      {
        if ( verbose )
          fprintf (stderr, "Using derived sample rate of %g over reported rate of %g\n",
                   samprate, mst->samprate);

        mst->samprate = samprate;
      }
      else
      {
        fprintf (stderr, "[%s.%s.%s.%s] Reported sample rate different than derived rate (%g versus %g)\n",
                 mst->network, mst->station, mst->location, mst->channel,
                 mst->samprate, samprate);
        fprintf (stderr, "   Consider using the -dr option to use the sample rate derived from the series\n");
      }
    }
  }

  if ( verbose )
    fprintf (stderr, "Writing ASCII for %.8s.%.8s.%.8s.%.8s\n",
	     mst->network, mst->station, mst->location, mst->channel);

  /* Generate source name, ISO time string and time components */
  mst_srcname (mst, srcname, 1);
  ms_hptime2isotimestr (mst->starttime, timestr, 1);
  ms_hptime2btime (mst->starttime, &btime);
  ms_doy2md (btime.year, btime.day, &month, &mday);

  /* Set sample type description */
  if ( mst->sampletype == 'f' || mst-> sampletype == 'd' )
  {
    samptype = "FLOAT";
  }
  else if ( mst->sampletype == 'i' )
  {
    samptype = "INTEGER";
  }
  else if ( mst->sampletype == 'a' )
  {
    samptype = "ASCII";
  }
  else
  {
    fprintf (stderr, "Error, unrecognized sample type: '%c'\n",
             mst->sampletype);
    return -1;
  }

  /* Generate and open output file name if single file not being used */
  if ( ! ofp )
  {
    /* Create output file name: Net.Sta.Loc.Chan.Qual.Year-Month-DayTHourMinSec.Subsec.txt */
    snprintf (outfile, sizeof(outfile), "%s.%s.%s.%s.%c.%04d-%02d-%02dT%02d%02d%02d.%06d.txt",
              mst->network, mst->station, mst->location, mst->channel, mst->dataquality,
              btime.year, month, mday, btime.hour, btime.min, btime.sec,
              (int)(mst->starttime - (hptime_t)MS_HPTIME2EPOCH(mst->starttime) * HPTMODULUS) );

    /* Open output file */
    if ( (ofp = fopen (outfile, "wb")) == NULL )
    {
      fprintf (stderr, "Cannot open output file: %s (%s)\n",
               outfile, strerror(errno));
      return -1;
    }

    outname = outfile;
  }

  /* Header format:
   * "TIMESERIES Net_Sta_Loc_Chan_Qual, ## samples, ## sps, isotime, SLIST|TSPAIR, INTEGER|FLOAT|ASCII, Units" */

  if ( outformat == 1 || mst->sampletype == 'a' )
  {
    if ( verbose > 1 )
      fprintf (stderr, "Writing ASCII sample list file: %s\n", outname);

    /* Print header line */
    fprintf (ofp, "TIMESERIES %s, %lld samples, %g sps, %s, SLIST, %s, %s\n",
             srcname, (long long int)mst->numsamples, mst->samprate, timestr, samptype, unitsstr);

    lines = (mst->numsamples / slistcols) + ((slistcols == 1) ? 0 : 1);

    if ( (samplesize = ms_samplesize(mst->sampletype)) == 0 )
    {
      fprintf (stderr, "Unrecognized sample type: %c\n", mst->sampletype);
    }

    if ( mst->sampletype == 'a' )
    {
      fwrite (mst->datasamples, (size_t) mst->numsamples, 1, ofp);
      fprintf (ofp, "\n");
    }
    else
      for ( cnt = 0, line = 0; line < lines; line++ )
      {
        for ( col = 1; col <= slistcols ; col ++ )
        {
          if ( cnt < mst->numsamples )
          {
            sptr = (char*)mst->datasamples + (cnt * samplesize);

            if ( mst->sampletype == 'i' )
            {
              if ( col != slistcols )
                fprintf (ofp, "%-10d  ", *(int32_t *)sptr);
              else
                fprintf (ofp, "%d", *(int32_t *)sptr);
            }
            else if ( mst->sampletype == 'f' )
            {
              if ( col != slistcols )
                fprintf (ofp, "%-10.8g  ", *(float *)sptr);
              else
                fprintf (ofp, "%.8g", *(float *)sptr);
            }
            else if ( mst->sampletype == 'd' )
            {
              if ( col != slistcols )
                fprintf (ofp, "%-10.10g  ", *(double *)sptr);
              else
                fprintf (ofp, "%.10g", *(double *)sptr);
            }

            cnt++;
          }
        }
        fprintf (ofp, "\n");
      }
  }
  else if ( outformat == 2 )
  {
    hptime_t samptime = mst->starttime;
    double hpperiod = ( mst->samprate ) ? (HPTMODULUS / mst->samprate) : 0;

    if ( verbose > 1 )
      fprintf (stderr, "Writing ASCII time-sample pair file: %s\n", outname);

    /* Print header line */
    fprintf (ofp, "TIMESERIES %s, %lld samples, %g sps, %s, TSPAIR, %s, %s\n",
             srcname, (long long int)mst->numsamples, mst->samprate, timestr, samptype, unitsstr);

    if ( (samplesize = ms_samplesize(mst->sampletype)) == 0 )
    {
      fprintf (stderr, "Unrecognized sample type: %c\n", mst->sampletype);
    }

    for ( cnt = 0; cnt < mst->numsamples; cnt++ )
    {
      ms_hptime2isotimestr (samptime, timestr, 1);

      sptr = (char*)mst->datasamples + (cnt * samplesize);

      if ( mst->sampletype == 'i' )
        fprintf (ofp, "%s  %d\n", timestr, *(int32_t *)sptr);

      else if ( mst->sampletype == 'f' )
        fprintf (ofp, "%s  %.8g\n", timestr, *(float *)sptr);

      else if ( mst->sampletype == 'd' )
        fprintf (ofp, "%s  %.10g\n", timestr, *(double *)sptr);

      samptime = mst->starttime + (hptime_t)((cnt+1) * hpperiod);
    }
  }
  else
  {
    fprintf (stderr, "Error, unrecognized format: '%d'\n", outformat);
  }

  if ( outname == outfile )
  {
    fclose (ofp);
    ofp = 0;
  }

  fprintf (stderr, "Wrote %lld samples from %s to %s\n",
	   (long long int)mst->numsamples, srcname, outname);

  return mst->numsamples;
}  /* End of writeascii() */


/***************************************************************************
 * parameter_proc:
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure.
 ***************************************************************************/
static int
parameter_proc (int argcount, char **argvec)
{
  int optind;

  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
  {
    if (strcmp (argvec[optind], "-V") == 0)
    {
      fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);
      exit (0);
    }
    else if (strcmp (argvec[optind], "-h") == 0)
    {
      usage();
      exit (0);
    }
    else if (strncmp (argvec[optind], "-v", 2) == 0)
    {
      verbose += strspn (&argvec[optind][1], "v");
    }
    else if (strcmp (argvec[optind], "-r") == 0)
    {
      reclen = strtoul (getoptval(argcount, argvec, optind++, 0), NULL, 10);
    }
    else if (strcmp (argvec[optind], "-dr") == 0)
    {
      deriverate = 1;
    }
    else if (strcmp (argvec[optind], "-i") == 0)
    {
      indifile = 1;
    }
    else if (strcmp (argvec[optind], "-tt") == 0)
    {
      timetol = strtod (getoptval(argcount, argvec, optind++, 0), NULL);
    }
    else if (strcmp (argvec[optind], "-rt") == 0)
    {
      sampratetol = strtod (getoptval(argcount, argvec, optind++, 0), NULL);
    }
    else if (strcmp (argvec[optind], "-o") == 0)
    {
      outputfile = getoptval(argcount, argvec, optind++, 1);
    }
    else if (strcmp (argvec[optind], "-f") == 0)
    {
      outformat = strtoul (getoptval(argcount, argvec, optind++, 0), NULL, 10);
    }
    else if (strcmp (argvec[optind], "-c") == 0)
    {
      slistcols = strtoul (getoptval(argcount, argvec, optind++, 0), NULL, 10);
    }
    else if (strcmp (argvec[optind], "-u") == 0)
    {
      unitsstr = getoptval(argcount, argvec, optind++, 0);
    }
    else if (strncmp (argvec[optind], "-", 1) == 0 &&
             strlen (argvec[optind]) > 1 )
    {
      fprintf(stderr, "Unknown option: %s\n", argvec[optind]);
      exit (1);
    }
    else
    {
      /* Add the file name to the intput file list */
      if ( ! addnode (&filelist, NULL, 0, argvec[optind], strlen(argvec[optind])+1) )
      {
        fprintf (stderr, "Error adding file name to list\n");
      }
    }
  }

  /* Make sure an input files were specified */
  if ( filelist == 0 )
  {
    fprintf (stderr, "No input files were specified\n\n");
    fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
    fprintf (stderr, "Try %s -h for usage\n", PACKAGE);
    exit (1);
  }

  /* Report the program version */
  if ( verbose )
    fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);

  /* Check the input files for any list files, if any are found
   * remove them from the list and add the contained list */
  if ( filelist )
  {
    struct listnode *prevln, *ln;
    char *lfname;

    prevln = ln = filelist;
    while ( ln != 0 )
    {
      lfname = ln->data;

      if ( *lfname == '@' )
      {
        /* Remove this node from the list */
        if ( ln == filelist )
          filelist = ln->next;
        else
          prevln->next = ln->next;

        /* Skip the '@' first character */
        if ( *lfname == '@' )
          lfname++;

        /* Read list file */
        readlistfile (lfname);

        /* Free memory for this node */
        if ( ln->key )
          free (ln->key);
        free (ln->data);
        free (ln);
      }
      else
      {
        prevln = ln;
      }

      ln = ln->next;
    }
  }

  return 0;
}  /* End of parameter_proc() */


/***************************************************************************
 * getoptval:
 * Return the value to a command line option; checking that the value is
 * itself not an option (starting with '-') and is not past the end of
 * the argument list.
 *
 * argcount: total arguments in argvec
 * argvec: argument list
 * argopt: index of option to process, value is expected to be at argopt+1
 * dasharg: flag indicating if the value can beging with a dash (-)
 *
 * Returns value on success and exits with error message on failure
 ***************************************************************************/
static char *
getoptval (int argcount, char **argvec, int argopt, int dasharg)
{
  if ( argvec == NULL || argvec[argopt] == NULL ) {
    fprintf (stderr, "getoptval(): NULL option requested\n");
    exit (1);
    return 0;
  }

  /* When the value potentially starts with a dash (-) */
  if ( (argopt+1) < argcount && dasharg )
    return argvec[argopt+1];

  /* Otherwise check that the value is not another option */
  if ( (argopt+1) < argcount && *argvec[argopt+1] != '-' )
    return argvec[argopt+1];

  fprintf (stderr, "Option %s requires a value\n", argvec[argopt]);
  exit (1);
  return 0;
}  /* End of getoptval() */


/***************************************************************************
 * readlistfile:
 *
 * Read a list of files from a file and add them to the filelist for
 * input data.  The filename is expected to be the last
 * space-separated field on the line.
 *
 * Returns the number of file names parsed from the list or -1 on error.
 ***************************************************************************/
static int
readlistfile (char *listfile)
{
  FILE *fp;
  char  line[1024];
  char *ptr;
  int   filecnt = 0;

  char  filename[1024];
  char *lastfield = 0;
  int   fields = 0;
  int   wspace;

  /* Open the list file */
  if ( (fp = fopen (listfile, "rb")) == NULL )
  {
    if (errno == ENOENT)
    {
      fprintf (stderr, "Could not find list file %s\n", listfile);
      return -1;
    }
    else
    {
      fprintf (stderr, "Error opening list file %s: %s\n",
               listfile, strerror (errno));
      return -1;
    }
  }
  if ( verbose )
    fprintf (stderr, "Reading list of input files from %s\n", listfile);

  while ( (fgets (line, sizeof(line), fp)) !=  NULL)
  {
    /* Truncate line at first \r or \n, count space-separated fields
     * and track last field */
    fields = 0;
    wspace = 0;
    ptr = line;
    while ( *ptr )
    {
      if ( *ptr == '\r' || *ptr == '\n' || *ptr == '\0' )
      {
        *ptr = '\0';
        break;
      }
      else if ( *ptr != ' ' )
      {
        if ( wspace || ptr == line )
        {
          fields++; lastfield = ptr;
        }
        wspace = 0;
      }
      else
      {
        wspace = 1;
      }

      ptr++;
    }

    /* Skip empty lines */
    if ( ! lastfield )
      continue;

    if ( fields >= 1 && fields <= 3 )
    {
      fields = sscanf (lastfield, "%s", filename);

      if ( fields != 1 )
      {
        fprintf (stderr, "Error parsing file name from: %s\n", line);
        continue;
      }

      if ( verbose > 1 )
        fprintf (stderr, "Adding '%s' to input file list\n", filename);

      /* Add file name to the intput file list */
      if ( ! addnode (&filelist, NULL, 0, filename, strlen(filename)+1) )
      {
        fprintf (stderr, "Error adding file name to list\n");
      }

      filecnt++;

      continue;
    }
  }

  fclose (fp);

  return filecnt;
}  /* End readlistfile() */


/***************************************************************************
 * addnode:
 *
 * Add node to the specified list.
 *
 * Return a pointer to the added node on success and NULL on error.
 ***************************************************************************/
static struct listnode *
addnode (struct listnode **listroot, void *key, int keylen,
	 void *data, int datalen)
{
  struct listnode *lastlp, *newlp;

  if ( data == NULL )
  {
    fprintf (stderr, "addnode(): No data specified\n");
    return NULL;
  }

  lastlp = *listroot;
  while ( lastlp != 0 )
  {
    if ( lastlp->next == 0 )
      break;

    lastlp = lastlp->next;
  }

  /* Create new listnode */
  newlp = (struct listnode *) malloc (sizeof (struct listnode));
  memset (newlp, 0, sizeof (struct listnode));

  if ( key )
  {
    newlp->key = malloc (keylen);
    memcpy (newlp->key, key, keylen);
  }

  if ( data)
  {
    newlp->data = malloc (datalen);
    memcpy (newlp->data, data, datalen);
  }

  newlp->next = 0;

  if ( lastlp == 0 )
    *listroot = newlp;
  else
    lastlp->next = newlp;

  return newlp;
}  /* End of addnode() */


/***************************************************************************
 * usage:
 * Print the usage message and exit.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "%s version: %s\n\n", PACKAGE, VERSION);
  fprintf (stderr, "Convert miniSEED data to ASCII\n\n");
  fprintf (stderr, "Usage: %s [options] input1.mseed [input2.mseed ...]\n\n", PACKAGE);
  fprintf (stderr,
	   " ## Options ##\n"
	   " -V           Report program version\n"
	   " -h           Show this usage message\n"
	   " -v           Be more verbose, multiple flags can be used\n"
	   " -r bytes     Specify SEED record length in bytes, default: 4096\n"
	   " -dr          Use the sampling rate derived from the time stamps instead\n"
	   "                of the sample rate denoted in the input data\n"
	   " -i           Process each input file individually instead of merged\n"
           " -tt secs     Specify a time tolerance for continuous traces\n"
           " -rt diff     Specify a sample rate tolerance for continuous traces\n"
           " -o outfile   Specify the output file, default is segment files\n"
	   " -f format    Specify ASCII output format (default is 1):\n"
           "                1=Header followed by sample value list\n"
           "                2=Header followed by time-sample value pairs\n"
	   " -c cols      Number of columns for sample value list output (default is %d)\n"
	   " -u units     Specify units string for headers, default is 'Counts'\n"
	   "\n"
	   "A separate output file is written for each continuous input time-series\n"
	   "with file names of the form:\n"
	   "Net.Sta.Loc.Chan.Qual.YYYY-MM-DDTHHMMSS.FFFFFF.txt\n"
	   "\n", slistcols);
}  /* End of usage() */
