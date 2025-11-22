using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.ComponentModel.Design;
using System.Drawing;
using System.Windows;
using System.Windows.Forms;
using System.Data;
using System.Text;
using System.IO;
using System.Threading;
using Microsoft.Win32;
using System.Globalization;


namespace LogParser
{
    static class Program
    {
        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]

        static void Main()
        {
            const int MAX_FILTERS = 64;
            String[] Filters = new String[MAX_FILTERS];
            bool DisplayMatches = false;
            bool FilterStartTime = false;
            bool FilterEndTime = false;
            DateTime StartDateTime = new DateTime();
            DateTime EndDateTime = new DateTime();
            String tmpS = "";

            object tmpObj = new object();

            StartDateTime = DateTime.UtcNow.ToLocalTime();
            EndDateTime = DateTime.UtcNow.ToLocalTime();

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "sourcefile", "");
            if (tmpObj != null)
            {
                SourceFileName = tmpObj.ToString();
            }

            for (int ii = 0; ii < MAX_FILTERS; ii++)
            {
                tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "filter"+ii.ToString(), "");
                if (tmpObj != null)
                {
                    Filters[ii] = tmpObj.ToString();
                }
            }

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "DisplayMatches", "false");
            if (tmpObj != null)
            {
                DisplayMatches = (tmpObj.ToString()=="true" ? true : false);
            }

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "FilterStartTime", "false");
            if (tmpObj != null)
            {
                FilterStartTime = (tmpObj.ToString() == "true" ? true : false);
            }

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "FilterEndTime", "false");
            if (tmpObj != null)
            {
                FilterEndTime = (tmpObj.ToString() == "true" ? true : false);
            }
            
             tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "StartDateTime", "01/01/2000 12:00:00AM");
             if (tmpObj != null)
             {
                 tmpS = tmpObj.ToString();
                 StartDateTime = DateTime.Parse(tmpS);
             }

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "EndDateTime", "01/01/2999 12:00:00AM");
             if (tmpObj != null)
             {
                 tmpS = tmpObj.ToString();
                 EndDateTime = DateTime.Parse(tmpS);
             }

            {
                LogParserSetup setup = new LogParserSetup();

                setup.textBox1.Text = SourceFileName;

                setup.FilterStrings.Lines = Filters;

                setup.DisplayMatches.Checked = DisplayMatches;
                setup.FilterStartTime.Checked = FilterStartTime;
                setup.FilterEndTime.Checked = FilterEndTime;
                setup.StartDate.Value = StartDateTime;
                setup.EndDate.Value = EndDateTime;
               

                for (bool done = false; !done; )
                {
                    DialogResult res = setup.ShowDialog();

                    switch (res)
                    {
                        case DialogResult.OK:
                            done = true;
                            break;

                        case DialogResult.Cancel:
                            return;

                        default:
                        case DialogResult.None:
                            break;
                    }
                }

                SourceFileName = setup.textBox1.Text;
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "sourcefile", SourceFileName);

                for (int ii = 0; ii < MAX_FILTERS; ii++)
                    Filters[ii] = "";

                String[] tmpFilters = setup.FilterStrings.Lines;
                for (int ii = 0; ii<tmpFilters.Length; ii++)
                {
                    if (tmpFilters[ii].Length == 0)
                        continue;

                    if (ii >= MAX_FILTERS)
                    {
                        add_message(true, "Too many Filters");
                        break;
                    }

                    Filters[ii] = tmpFilters[ii].ToLower().Trim();
                }

                for (int ii = 0; ii < MAX_FILTERS; ii++)
                {
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "filter" + ii.ToString(), Filters[ii]);
                }

                DisplayMatches = setup.DisplayMatches.Checked;
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "DisplayMatches", (DisplayMatches ? "true" : "false") );

                FilterStartTime = setup.FilterStartTime.Checked;
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "FilterStartTime", (FilterStartTime? "true" : "false") );

                FilterEndTime = setup.FilterEndTime.Checked;
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "FilterEndTime", (FilterEndTime? "true" : "false") );

                StartDateTime = setup.StartDate.Value;
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "StartDateTime", StartDateTime.ToString());

                EndDateTime = setup.EndDate.Value;
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\LogParser", "EndDateTime", EndDateTime.ToString());
            }

            if (DisplayMatches)
            {
                add_message(false, "");
                add_message(false, "@@@@@  Start of Matching Messages  @@@@@");
                add_message(false, "");
            }

            DateTime FirstMatchTime = DateTime.Now;
            DateTime LastMatchTime = DateTime.Now;
            int MatchCount = 0;

 
            using (StreamReader sr = new StreamReader(SourceFileName))
            {
                String line = "";
                String msgDateTimeS = "";
                DateTime msgDateTime;
                bool firstmatch = false;
               
                while (sr.Peek() >= 0) 
                {
                    line = sr.ReadLine();
                    line = line.ToLower();

                    //verify that line has at least date & time
                    if ((line.Length < 15)  ||
                        (line[6] != ' ')    ||
                        (line[9] != ':')    ||
                        (line[12] != ':')   )
                        continue;

                    msgDateTimeS = line.Substring(2, 2) + "/" +
                                   line.Substring(4, 2) + "/" +
                                   line.Substring(0, 2) + " " +
                                   line.Substring(7, 2) + ":" +
                                   line.Substring(10, 2) + ":" +
                                   line.Substring(13, 2);

                    msgDateTime = DateTime.Parse(msgDateTimeS);
                    msgDateTime = msgDateTime.ToLocalTime();

                    //make sure line contains all filter strings
                    bool matches = true;
                    for(int ii=0; ii<Filters.Length; ii++)
                    {
                        if (!line.Contains(Filters[ii]))
                        {
                            matches = false;
                            break;
                        }    
                    }

                    if ( (FilterStartTime && msgDateTime < StartDateTime) ||
                         (FilterEndTime && msgDateTime > EndDateTime) )
                    {
                        matches = false;
                    }

                    if (matches)
                    {
                       if (!firstmatch)
                       {
                           firstmatch = true;
                           FirstMatchTime = msgDateTime;
                       }

                       LastMatchTime = msgDateTime;

                       MatchCount++;

                       if ( DisplayMatches )
                           add_message(false, msgDateTime.ToString().PadRight(23) + "| " + line);
                    }
                }
            }

            if (DisplayMatches)
            {
                add_message(false, "");
                add_message(false, "@@@@@  End of Matching Messages  @@@@@");
                add_message(false, "");
            }

            long DeltaTicks = LastMatchTime.Ticks - FirstMatchTime.Ticks;
            long DeltaSeconds = DeltaTicks / 10000000;
            double CountsPerSecond = (double)MatchCount / (double)DeltaSeconds;
            double CountsPerMinute = CountsPerSecond * 60.0;

            add_message(false, "Total Matches         : " + MatchCount.ToString());
            add_message(false, "First Match Time      : " + FirstMatchTime.ToString());
            add_message(false, "Last Match Time       : " + LastMatchTime.ToString());
            add_message(false, "Elapsed Time          : " + LastMatchTime.Subtract(FirstMatchTime));
            add_message(false, "Elapsed Seconds       : " + DeltaSeconds.ToString());
            add_message(false, "Avg counts per second : " + CountsPerSecond.ToString(".####"));
            add_message(false, "Avg counts per minute : " + CountsPerMinute.ToString(".####"));

            if (messages.Visible)
            {
                messages.Visible = false;
                messages.ShowDialog();
            }
        }

        static String SourceFileName = "";
        static OpenFileDialog dlgsrc = new OpenFileDialog();

        static Form1 messages = new Form1();
        //static bool error_flag = false;

        static void add_message(bool error, String msg)
        {
            //if (error) error_flag = true;
            messages.textBox1.AppendText(msg+"\r\n");

            messages.Show();
            messages.Update();
        }
 
    }
}