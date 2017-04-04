# <p >miniSEED to ASCII converter</p>

1. [Name](#)
1. [Synopsis](#synopsis)
1. [Description](#description)
1. [Options](#options)
1. [List Files](#list-files)
1. [Author](#author)

## <a id='synopsis'>Synopsis</a>

<pre >
mseed2ascii [options] file1 [file2 file3 ...]
</pre>

## <a id='description'>Description</a>

<p ><b>mseed2ascii</b> converts miniSEED time-series data to ASCII format. By default all aspects of the input files are automatically detected (record length, byte order, etc.).  If an input file name is prefixed with an '@' character the file is assumed to contain a list of input data files, see <i>LIST FILES</i> below.</p>

<p >A separate output file is written for each continuous time-series in the input data.  Output file names are of the form:</p>

<pre >
"Net.Sta.Loc.Chan.Qual.YYYY-MM-DDTHHMMSS.FFFFFF.txt"
</pre>

<pre >
For example:
"TA.ELFS..LHZ.R.2006-04-22T153619.000000.txt"
</pre>

<p >If the input file name is "-" input miniSEED records will be read from standard input.</p>

## <a id='options'>Options</a>

<b>-V</b>

<p style="padding-left: 30px;">Print program version and exit.</p>

<b>-h</b>

<p style="padding-left: 30px;">Print program usage and exit.</p>

<b>-v</b>

<p style="padding-left: 30px;">Be more verbose.  This flag can be used multiple times ("-v -v" or "-vv") for more verbosity.</p>

<b>-r </b><i>bytes</i>

<p style="padding-left: 30px;">Specify the miniSEED record length in <i>bytes</i>, by default this is autodetected.</p>

<b>-dr</b>

<p style="padding-left: 30px;">Use the sampling rate derived from the start and end times and the number of samples instead of the rate specified in the input data. This is useful when the sample rate in the input data does not have enough resolution to represent the true rate.</p>

<b>-i</b>

<p style="padding-left: 30px;">Process each input file individually.  By default all input files are read and all data is buffered in memory before ASCII files are written. This allows time-series spanning mutilple input files to be merged and written in a single ASCII file.  The intention is to use this option when processing large amounts of data in order to keep memory usage within reasonable limits.</p>

<b>-tt </b><i>secs</i>

<p style="padding-left: 30px;">Specify a time tolerance for constructing continous trace segments. The tolerance is specified in seconds.  The default tolerance is 1/2 of the sample period.</p>

<b>-rt </b><i>diff</i>

<p style="padding-left: 30px;">Specify a sample rate tolerance for constructing continous trace segments.  The tolerance is specified as the difference between two sampling rates.  The default tolerance is tested as: (abs(1-sr1/sr2) < 0.0001).</p>

<b>-o </b><i>outfile</i>

<p style="padding-left: 30px;">Write all ASCII output to <i>outfile</i>, if <i>outfile</i> is a single dash (-) then all output will go to stdout.  If this option is not specified each contiguous segment is written to a separate file.  All diagnostic output from the program is written to stderr and should never get mixed with data going to stdout.</p>

<b>-f </b><i>format</i>

<p style="padding-left: 30px;">The default output format is sample list.  This option applies to all output files:</p>

<pre style="padding-left: 30px;">
1 : Sample list format, header includes time stamp of first
2 : Time-sample pair format, each sample value listed with time stamp
</pre>

<b>-c </b><i>cols</i>

<p style="padding-left: 30px;">Specify the number of columns to use for sample list formatted output, default is 1 column.</p>

<b>-u </b><i>units</i>

<p style="padding-left: 30px;">Specify the units string that should be included in the ASCII output headers, the default is "Counts".</p>

## <a id='list-files'>List Files</a>

<p >If an input file is prefixed with an '@' character the file is assumed to contain a list of file for input.  Multiple list files can be combined with multiple input files on the command line.  The last, space separated field on each line is assumed to be the file name to be read.</p>

<p >An example of a simple text list:</p>

<pre >
TA.ELFS..LHE.R.mseed
TA.ELFS..LHN.R.mseed
TA.ELFS..LHZ.R.mseed
</pre>

## <a id='author'>Author</a>

<pre >
Chad Trabant
IRIS Data Management Center
</pre>


(man page 2013/10/06)
