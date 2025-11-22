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
using Excel;

namespace xls2xptables
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            String spaces = "                                                                                                                                            ";
            const int NUM_LEVELS = 500; // this is just a safety now. The script will export as much data as the spreadsheet has
            const int indentstep = 4;
            const int NUM_ADJUSTMENTS = 11;


            object tmpObj = new object();

            String SourceFileName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2xptables", "sourcefile", "");
            if (tmpObj != null)
            {
                SourceFileName = tmpObj.ToString();
            }

            String DestFileName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2xptables", "destfile", "");
            if (tmpObj != null)
            {
                DestFileName = tmpObj.ToString();
            }

            //source excel file - SourceFileName
            //dest file name - DestFileName

            bool prompt_for_files = true;

            if ((SourceFileName != "") &&
                 (DestFileName != ""))
            {
                String msg = "";

                msg = "Excel File:          " + SourceFileName + "\n" +
                      "Destination File:    " + DestFileName + "\n" +
                      "Are these selections OK ?";

                if (MessageBox.Show(msg, "Selected Files/Dirs", MessageBoxButtons.YesNo) == DialogResult.Yes)
                {
                    prompt_for_files = false;
                }
            }


            if (prompt_for_files)
            {
                {
                    OpenFileDialog dlgsrc = new OpenFileDialog();
                    dlgsrc.Title = "Choose source Excel file";
                    dlgsrc.CheckFileExists = true;
                    dlgsrc.DefaultExt = "xls";
                    dlgsrc.Filter = "Excel files (*.xls)|*.xls|Excel 2007 files (*.xlsx, *.xlsm)|*.xlsx;*.xlsm";
                    dlgsrc.InitialDirectory = SourceFileName;
                    if (dlgsrc.ShowDialog() != DialogResult.OK)
                    {
                        return;
                    }

                    SourceFileName = dlgsrc.FileName;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2xptables", "sourcefile", SourceFileName);
                }

                {
                    OpenFileDialog dlgsrc = new OpenFileDialog();
                    dlgsrc.Title = "Choose destination data file";
                    dlgsrc.CheckFileExists = false;
                    dlgsrc.DefaultExt = "data";
                    dlgsrc.Filter = "data files (*.data)|*.data";
                    dlgsrc.InitialDirectory = DestFileName;
                    if (dlgsrc.ShowDialog() != DialogResult.OK)
                    {
                        return;
                    }

                    DestFileName = dlgsrc.FileName;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2xptables", "destfile", DestFileName);
                }
            }
            DataSet result = null;
            ExcelDataReader.ExcelDataReader spreadsheet = null;
            IExcelDataReader excelReader = null;
            bool excel2007mode = false;
            if (SourceFileName.EndsWith(".xls"))
            {
                 FileStream fs = new FileStream(SourceFileName, FileMode.Open, FileAccess.Read, FileShare.Read);
                 spreadsheet = new ExcelDataReader.ExcelDataReader(fs);
                 fs.Close();
              }
            else
            {
                excel2007mode = true;
                FileStream stream = File.Open(SourceFileName, FileMode.Open, FileAccess.Read);
                // Reading from a binary Excel file ('97-2003 format; *.xls)
                excelReader = Factory.CreateReader(stream, ExcelFileType.OpenXml);
                // Reading from a OpenXml Excel file (2007 format; *.xlsx)
                //IExcelDataReader excelReader = Factory.CreateReader(stream, ExcelFileType.OpenXml);

                // The result of each spreadsheet will be created in the result.Tables
                result = excelReader.AsDataSet();
            }




            if (File.Exists(DestFileName) &&
             ((File.GetAttributes(DestFileName) & FileAttributes.ReadOnly) != 0))
            {
                add_message(true, DestFileName + " is Read Only (not checked out), skipping export");
                goto exit;
            }
        
            using (StreamWriter sw = new StreamWriter(DestFileName))
            {
                int indent = 0;

                sw.WriteLine(spaces.Substring(0, indent) + "{");
                indent += indentstep;
                
                //loop for all worksheets
                DataTableCollection theTables = null;

                if (excel2007mode)
                    theTables = result.Tables;
                else
                    theTables = spreadsheet.WorkbookData.Tables;

                foreach (DataTable dt in theTables)
                {
                    if (dt.TableName.ToString().StartsWith("_"))
                        continue;

                    String tmpname = dt.TableName.ToString();

                    //Handle custom charts
                    if (tmpname.ToLower().StartsWith("master") && tmpname.ToLower().EndsWith("chart"))
                    {
                        //Found a chart, get the value
                        String sValue = tmpname.Substring(6, tmpname.Length - 11).Trim();//.ToUpper();

                        int numLabels = 0;
                        int[] label_row = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
                        int[] label_col = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

                        //scan sheet for labels
                        for (int row = 0; row < dt.Rows.Count; row++)
                        {
                            for (int col = 0; col < dt.Columns.Count; col++)
                            {
                                if (dt.Rows[row][col].ToString().StartsWith("PARSE"))
                                {
                                    label_row[numLabels] = row;
                                    label_col[numLabels] = col;
                                    numLabels++;
                                }
                            }
                        }



                        add_message(false, "Exported " + numLabels + " " + sValue + " Tables");

                        string rank, subrank, name;
                        for (int aa = 0; aa < numLabels; aa++)
                        {
                            string[] substrings = dt.Rows[label_row[aa]][label_col[aa]].ToString().Split(' ');
                            int shouldIndex;
                            bool bParseAdjustments = true;
                            bool bAddLeadingZero = false;

                            if (substrings.Length < 3)
                            {
                                if (substrings[1] == "Mission")
                                {
                                    name = sValue.ToUpper() + "_MISSION";
                                    rank = "Henchman";
                                    subrank = "Normal";
                                    shouldIndex = 0;
                                }
                                else if (substrings[1] == "Required")
                                {
                                    name = sValue.ToUpper() + "_REQUIRED";
                                    rank = "Henchman";
                                    subrank = "Normal";
                                    shouldIndex = 0;
                                    bParseAdjustments = false;
                                    bAddLeadingZero = true;
                                }
                                else if (substrings[1] == "SuperCritterPet")
                                {
                                    name = sValue.ToUpper() + "_REQUIRED_SCP";
                                    rank = "Henchman";
                                    subrank = "Normal";
                                    shouldIndex = 0;
                                    bParseAdjustments = false;
                                    bAddLeadingZero = true;
                                }
                                else
                                {
                                    rank = substrings[1];
                                    subrank = "Normal";
                                    name = (sValue.ToUpper() + "_" + rank.ToUpper() + "_" + subrank.ToUpper());
                                    shouldIndex = 1;
                                }
                            }
                            else
                            {
                                rank = substrings[1];
                                subrank = substrings[2];
                                name = (sValue.ToUpper() + "_" + rank.ToUpper() + "_" + subrank.ToUpper());
                                shouldIndex = 1;
                            }


                            sw.Write("RewardValTable " + name + " " + rank + " " + subrank + " " + sValue);
                            sw.WriteLine("");
                            sw.Write("{");
                            sw.WriteLine("");
                            sw.WriteLine("\tshouldIndex " + shouldIndex + ", ");
                            sw.Write("\tVal ");

                            //!!!! hack for main level XP table
                            //add in a 0 entry at start of table
                            //design skewed table in XLS so that first entry is amount of XP for Level 2
                            //for them to change XLS would require a change to 100s of linked formulas
                            if (bAddLeadingZero)
                                sw.Write("0, ");

                            int iLimit = dt.Rows.Count - label_row[aa] - 1;
                            for (int ii = 0; ii < Math.Min(NUM_LEVELS, iLimit); ii++)
                            {
                                int row = label_row[aa] + 1 + ii;
                                int col = label_col[aa];

                                float val = 0.0f;
                                if (dt.Rows[row][col].ToString().Length == 0)
                                    break;

                                val = float.Parse(dt.Rows[row][col].ToString());
                                int intval = (int)(val + 0.5);

                                sw.Write(intval.ToString() + ", ");

                                //!!!! hack for main level XP table
                                // Since this is a hack, the designers can screw it up by mucking up the spreadsheet
                                if ((aa == 0) &&
                                    (ii == iLimit-3))
                                    break;
                            }
                            if (!bParseAdjustments)
                            {
                                sw.WriteLine("");
                                sw.WriteLine("\tAdj 0,");
                                sw.WriteLine("}");
                                sw.WriteLine("");
                                sw.WriteLine("");
                            }
                            else
                            {
                                sw.WriteLine("");
                                sw.Write("\tAdj ");
                                for (int ii = 0; ii < NUM_ADJUSTMENTS; ii++)
                                {
                                    int row = label_row[aa] - 1;
                                    int col = label_col[aa] - 5 + ii;

                                    float val = float.Parse(dt.Rows[row][col].ToString());

                                    sw.Write(val.ToString() + ", ");
                                }
                                sw.WriteLine("");
                                sw.WriteLine("}");
                                sw.WriteLine("");
                                sw.WriteLine("");
                            }
                        }
                    }

                    else if (tmpname.ToLower() == "misc xp rp res chart")
                    {
                        int numLabels = 0;
                        int[] label_row = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
                        int[] label_col = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };

                        //scan sheet for labels
                        for (int row = 0; row < dt.Rows.Count; row++)
                        {
                            for (int col = 0; col < dt.Columns.Count; col++)
                            {
                                if (dt.Rows[row][col].ToString().StartsWith("PARSE"))
                                {
                                    label_row[numLabels] = row;
                                    label_col[numLabels] = col;
                                    numLabels++;
                                }
                            }
                        }

                        add_message(false, "Exported " + numLabels + " Misc Tables");

                        
                        for (int aa = 0; aa < numLabels; aa++)
                        {
                            string[] substrings = dt.Rows[label_row[aa]][label_col[aa]].ToString().Split(' ');

                            sw.WriteLine("RewardValTable " + substrings[1] + " Henchman Weak Misc");
                            sw.WriteLine("{");
                            sw.WriteLine("\tshouldIndex 0, ");
                            sw.Write("\tVal ");

                            for (int ii = 0; ii < Math.Min(NUM_LEVELS,dt.Rows.Count - label_row[aa] - 1); ii++)
                            {
                                int row = label_row[aa] + 1 + ii;
                                int col = label_col[aa];

                                float val = 0.0f;
                                if (dt.Rows[row][col].ToString().Length == 0)
                                    break;

                                val = float.Parse(dt.Rows[row][col].ToString());
                                int intval = (int)(val + 0.5);

                                sw.Write(intval.ToString() + ", ");

                            }

                            sw.WriteLine("");
                            sw.Write("\tAdj ");
                            for (int ii = 0; ii < NUM_ADJUSTMENTS; ii++)
                            {
                                sw.Write("1, ");
                            }
                            sw.WriteLine("");
                            sw.WriteLine("}");
                            sw.WriteLine("");
                            sw.WriteLine("");
                        }
                    }

                    else if (tmpname.ToLower() == "stars")
                    {
                        int numLabels = 0;
                        int[] label_row = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
                        int[] label_col = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
                        string commonAdjust = "Adj ";
                        //scan sheet for labels
                        for (int row = 0; row < dt.Rows.Count; row++)
                        {
                            for (int col = 0; col < dt.Columns.Count; col++)
                            {
                                if (dt.Rows[row][col].ToString().StartsWith("PARSE"))
                                {
                                    label_row[numLabels] = row;
                                    label_col[numLabels] = col;
                                    numLabels++;
                                }
                                else if (dt.Rows[row][col].ToString() == "Level Adjust")
                                {
                                    for (int ii = 0; ii < NUM_ADJUSTMENTS; ii++)
                                    {
                                        commonAdjust += dt.Rows[row + ii + 1][col].ToString() + ", ";
                                    }
                                }
                            }
                        }

                        add_message(false, "Exported " + numLabels + " Stars Tables");


                        string rank, subrank, name;
                        int shouldIndex;
                        for (int aa = 0; aa < numLabels; aa++)
                        {
                            shouldIndex = 1;
                            rank = dt.Rows[label_row[aa]][label_col[aa] + 1].ToString();
                            if (rank == "")
                                rank = dt.Rows[label_row[aa] - label_row[aa]%3][label_col[aa] + 1].ToString();
                            subrank = dt.Rows[label_row[aa]][label_col[aa] + 2].ToString();
                            name = ("STAR_" + rank.ToUpper());
                            if (subrank != "")
                                name += "_" + subrank.ToUpper();
                            else
                                subrank = "Normal";

                            if (rank == "Misc" || rank == "Mission")//quick hack for misc/mission data at the end
                            {
                                rank = "Henchman";
                                shouldIndex = 0;
                            }

                            sw.Write("RewardValTable " + name + " " + rank + " " + subrank + " Star");
                            sw.WriteLine("");
                            sw.Write("{");
                            sw.WriteLine("");
                            sw.WriteLine("\tshouldIndex " + shouldIndex + ", ");
                            sw.Write("\tVal " + dt.Rows[label_row[aa]][label_col[aa] + 4].ToString() + ", ");
                            sw.WriteLine("");
                            sw.WriteLine("\t"+commonAdjust);
                            sw.WriteLine("}");
                            sw.WriteLine("");
                            sw.WriteLine("");
                        }
                    }
                
                }

                indent -= indentstep;
                sw.WriteLine(spaces.Substring(0, indent) + "}");
                sw.WriteLine("");

                sw.Close();
            }

        exit:
            if (excel2007mode)
                excelReader.Close();
            if (error_flag)
            {
                Console.Beep(500, 75);
                Thread.Sleep(50);
                Console.Beep(250, 75);
            }

            if (messages.Visible)
            {
                messages.Visible = false;
                messages.ShowDialog();
            }
        }

        static Form1 messages = new Form1();
        static bool error_flag = false;

        static void add_message(bool error, String msg)
        {
            if (error) error_flag = true;

            messages.listBox1.Items.Insert(messages.listBox1.Items.Count, msg);
            messages.Show();
            messages.Update();
        }
        
    }
}