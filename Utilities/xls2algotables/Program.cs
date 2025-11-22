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

namespace xls2algotables
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            String spaces = "                                                                                                                                            ";
            const int NUM_LEVELS = 50;
            const int NUM_QUALITIES = 5;
            const int NUM_COMPTYPES = 5;
            const int indentstep = 4;


            object tmpObj = new object();

            String SourceFileName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2algotables", "sourcefile", "");
            if (tmpObj != null)
            {
                SourceFileName = tmpObj.ToString();
            }

            String DestFileName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2algotables", "destfile", "");
            if (tmpObj != null)
            {
                DestFileName = tmpObj.ToString();
            }

            String ItemVarFileName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2algotables", "itemvarfile", "");
            if (tmpObj != null)
            {
                ItemVarFileName = tmpObj.ToString();
            }

            bool prompt_for_files = true;

            if ((SourceFileName != "") &&
                 (DestFileName != "") &&
                 (ItemVarFileName != ""))
            {
                String msg = "";

                msg = "Excel File:       " + SourceFileName + "\n" +
                      "Destination File: " + DestFileName + "\n" +
                      "ItemVar File:     " + ItemVarFileName + "\n\n" +
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
                    dlgsrc.Filter = "Excel files (*.xls)|*.xls|Excel 2007 files (*.xlsx)|*.xlsx|Excel 2007 Macro-enabled files (*.xlsm)|*.xlsm";
                    dlgsrc.InitialDirectory = SourceFileName;
                    if (dlgsrc.ShowDialog() != DialogResult.OK)
                    {
                        return;
                    }

                    SourceFileName = dlgsrc.FileName;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2algotables", "sourcefile", SourceFileName);
                }

                {
                    OpenFileDialog dlgsrc = new OpenFileDialog();
                    dlgsrc.Title = "Choose destination algo data file";
                    dlgsrc.CheckFileExists = false;
                    dlgsrc.DefaultExt = "data";
                    dlgsrc.Filter = "data files (*.data)|*.data";
                    dlgsrc.InitialDirectory = DestFileName;
                    if (dlgsrc.ShowDialog() != DialogResult.OK)
                    {
                        return;
                    }

                    DestFileName = dlgsrc.FileName;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2algotables", "destfile", DestFileName);
                }

                {
                    OpenFileDialog dlgsrc = new OpenFileDialog();
                    dlgsrc.Title = "Choose destination itemvar file";
                    dlgsrc.CheckFileExists = false;
                    dlgsrc.DefaultExt = "itemvar";
                    dlgsrc.Filter = "itemvar files (*.itemvar)|*.itemvar";
                    dlgsrc.InitialDirectory = ItemVarFileName;
                    if (dlgsrc.ShowDialog() != DialogResult.OK)
                    {
                        return;
                    }

                    ItemVarFileName = dlgsrc.FileName;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2algotables", "itemvarfile", ItemVarFileName);
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
            }
            else
            {
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
                        bool error_happened = false;

                        if (dt.TableName.ToString().StartsWith("_"))
                            continue;

                        String tmpname = dt.TableName.ToString().ToLower();


                        if (tmpname == "final drop rate")
                        {
                            int NUM_LABELS = 4;
                            String[] label = { "henchman",
                                           "villain",
                                           "mastervillain",
                                           "supervillain" };
                            int[] label_row = { -1, -1, -1, -1, };
                            int[] label_col = { -1, -1, -1, -1, };
                            bool[] label_found = { false, false, false, false, };

                            //scan sheet for labels
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                bool done = false;

                                for (int row = 0; row < dt.Rows.Count && !done; row++)
                                {
                                    for (int col = 0; col < dt.Columns.Count && !done; col++)
                                    {
                                        if (dt.Rows[row][col].ToString().ToLower().Trim() == label[label_idx])
                                        {
                                            label_row[label_idx] = row;
                                            label_col[label_idx] = col;
                                            label_found[label_idx] = true;
                                            done = true;
                                        }
                                    }
                                }
                            }

                            //round off floats to 8 decimal places to prevent outputting in e^x notation
                            double parsedval = 0;
                            for (int row = 0; row < dt.Rows.Count; row++)
                            {
                                for (int col = 0; col < dt.Columns.Count; col++)
                                {
                                    parsedval = 0;
                                    if (dt.Rows[row][col].GetType().Name == "String")
                                        if (double.TryParse(dt.Rows[row][col].ToString(), out parsedval))
                                        {
                                            dt.Rows[row][col] = Math.Round(parsedval, 8);
                                        }
                                }
                            }

                            //verify that all labels are found
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                if (!label_found[label_idx])
                                {
                                    add_message(true, "Label " + label_found[label_idx] + "is not found in worksheet " + tmpname);
                                    error_happened = true;
                                }
                            }

                            if (error_happened)
                            {
                                add_message(true, "skipping export of algo tables");
                            }
                            else
                            {
                                add_message(false, "Exported Algo Tables");

                                sw.WriteLine("// each line of five entries is per level, each entry is the weight number out of 10000");
                                sw.WriteLine("// white,yellow,green,blue,purple");
                                sw.WriteLine("");

                                for (int aa = 0; aa < NUM_LABELS; aa++)
                                {
                                    sw.WriteLine(spaces.Substring(0, indent) + "AlgoTable " + label[aa]);
                                    sw.WriteLine(spaces.Substring(0, indent) + "{");
                                    indent += indentstep;

                                    for (int ii = 0; ii < NUM_LEVELS; ii++)
                                    {
                                        int row = label_row[aa] + 3 + ii;

                                        sw.WriteLine(spaces.Substring(0, indent) + "AlgoEntry");
                                        sw.WriteLine(spaces.Substring(0, indent) + "{");
                                        indent += indentstep;

                                        sw.Write(spaces.Substring(0, indent));
                                        sw.Write("weight\t");

                                        for (int jj = 0; jj < 5; jj++)
                                        {
                                            int col = label_col[aa] + jj;
                                            String tmpS = dt.Rows[row][col].ToString();

                                            tmpS = tmpS.Replace('%', ' ');
                                            tmpS = tmpS.Trim();
                                            double val = double.Parse(tmpS);

                                            val = val *= 10000;

                                            sw.Write(val.ToString("f0") + ",\t");
                                        }
                                        sw.Write("\t\t// {0}", ii + 1);
                                        sw.WriteLine("");

                                        indent -= indentstep;
                                        sw.WriteLine(spaces.Substring(0, indent) + "}");
                                    }

                                    indent -= indentstep;
                                    sw.WriteLine(spaces.Substring(0, indent) + "}");
                                    sw.WriteLine("");

                                }

                                sw.WriteLine("");
                                sw.WriteLine("");
                            }
                        }

						else if (tmpname == "algo item power scale")
						{
							int NUM_LABELS = 1;
							String[] label = {"rewards"};
							int[] label_row = {-1};
							int[] label_col = {-1};
							bool[] label_found = {false};

							// scan sheet for labels
							for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
							{
								for (int row = 0; row < dt.Rows.Count && !label_found[label_idx]; row++)
								{
									for (int col = 0; col < dt.Columns.Count && !label_found[label_idx]; col++)
									{
										if (dt.Rows[row][col].ToString().ToLower().Trim() == label[label_idx])
										{
											label_row[label_idx] = row;
											label_col[label_idx] = col;
											label_found[label_idx] = true;
										}
									}
								}
							}

							// verify that all labels are found
							for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
							{
								if (!label_found[label_idx])
								{
									add_message(true, "Label " + label_found[label_idx] + "is not found in worksheet " + tmpname);
									error_happened = true;
								}
							}

							if (error_happened)
							{
								add_message(true, "skipping export of Algo Item Power Adjust");
							}
							else
							{
								float val = float.Parse(dt.Rows[label_row[0] + 1][label_col[0]].ToString().Trim());
								add_message(false, "Exported Algo Item Power Scale");

								sw.WriteLine(spaces.Substring(0, indent) + "AlgoItemPowerScale " + val.ToString());
							}
						}                       
                    }

                    indent -= indentstep;
                    sw.WriteLine(spaces.Substring(0, indent) + "}");

                    sw.Close();
                }
            }

            if (File.Exists(ItemVarFileName) &&
                                      ((File.GetAttributes(ItemVarFileName) & FileAttributes.ReadOnly) != 0))
            {
                add_message(true, ItemVarFileName + " is Read Only (not checked out), skipping export");
            }
            else
            {
                using (StreamWriter itemvar_sw = new StreamWriter(ItemVarFileName))
                {
                    int indent = 0;

                    //loop for all worksheets
                    DataTableCollection theTables = null;

                    if (excel2007mode)
                        theTables = result.Tables;
                    else
                        theTables = spreadsheet.WorkbookData.Tables;

                    foreach (DataTable dt in theTables)
                    {
                        bool error_happened = false;

                        if (dt.TableName.ToString().StartsWith("_"))
                            continue;

                        String tmpname = dt.TableName.ToString().ToLower();
                  
                        if (tmpname == "itemvars")
                        {
                            const int NUM_LABELS = 1;
                            const int ITEMVARS = 0;
                            String[] label = { "itemvars" };
                            int[] label_row = { -1, };
                            int[] label_col = { -1, };
                            bool[] label_found = { false, };

                            //scan sheet for labels
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                bool done = false;

                                for (int row = 0; row < dt.Rows.Count && !done; row++)
                                {
                                    for (int col = 0; col < dt.Columns.Count && !done; col++)
                                    {
                                        if (dt.Rows[row][col].ToString().ToLower().Trim() == label[label_idx])
                                        {
                                            label_row[label_idx] = row;
                                            label_col[label_idx] = col;
                                            label_found[label_idx] = true;
                                            done = true;
                                        }
                                    }
                                }
                            }

                            //verify that all labels are found
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                if (!label_found[label_idx])
                                {
                                    add_message(true, "Label " + label_found[label_idx] + "is not found in worksheet " + tmpname);
                                    error_happened = true;
                                }
                            }

                            if (error_happened)
                            {
                                add_message(true, "skipping export of ItemVars");
                            }
                            else
                            {
                                if (File.Exists(ItemVarFileName) &&
                                     ((File.GetAttributes(ItemVarFileName) & FileAttributes.ReadOnly) != 0))
                                {
                                    add_message(true, ItemVarFileName + " is Read Only (not checked out), skipping export");
                                    continue;
                                }

                                add_message(false, "Exported " + ItemVarFileName);

                               
                                for (int row = label_row[ITEMVARS] + 1; row < dt.Rows.Count; row++)
                                {
                                    String var_name = dt.Rows[row][label_col[ITEMVARS]].ToString().Trim();

                                    if (var_name.StartsWith("_"))
                                        continue;

                                    if (var_name.Length == 0)
                                        break;

                                    itemvar_sw.WriteLine(spaces.Substring(0, indent) + "ItemVar {0}", var_name);
                                    itemvar_sw.WriteLine(spaces.Substring(0, indent) + "{");
                                    indent += indentstep;
                                    itemvar_sw.Write(spaces.Substring(0, indent) + "Value FLT " + dt.Rows[row][label_col[ITEMVARS] + 1]);
                                    itemvar_sw.WriteLine("");
                                    indent -= indentstep;
                                    itemvar_sw.WriteLine(spaces.Substring(0, indent) + "}");
                                    itemvar_sw.WriteLine("");
                                }
                            }
                        }
                    }
                    itemvar_sw.Close();
                }               
            }


            String CommonDestFileName = DestFileName.Replace(".data", "_common.data");

            if (File.Exists(CommonDestFileName) &&
             ((File.GetAttributes(CommonDestFileName) & FileAttributes.ReadOnly) != 0))
            {
                add_message(true, CommonDestFileName + " is Read Only (not checked out), skipping export");
            }
            else
            {
                using (StreamWriter sw = new StreamWriter(CommonDestFileName))
                {
                    int indent = 0;

                    sw.WriteLine(spaces.Substring(0, indent) + "{");
                    indent += indentstep;

                    //loop for all worksheets
                    foreach (DataTable dt in spreadsheet.WorkbookData.Tables)
                    {
                        bool error_happened = false;

                        if (dt.TableName.ToString().StartsWith("_"))
                            continue;

                        String tmpname = dt.TableName.ToString().ToLower();

                        if (tmpname == "crafting count level adjust")
                        {
                            int NUM_LABELS = 1;
                            String[] label = { "crafting count adjust" };
                            int[] label_row = { -1, };
                            int[] label_col = { -1, };
                            bool[] label_found = { false, };

                            //scan sheet for labels
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                bool done = false;

                                for (int row = 0; row < dt.Rows.Count && !done; row++)
                                {
                                    for (int col = 0; col < dt.Columns.Count && !done; col++)
                                    {
                                        if (dt.Rows[row][col].ToString().ToLower().Trim() == label[label_idx])
                                        {
                                            label_row[label_idx] = row;
                                            label_col[label_idx] = col;
                                            label_found[label_idx] = true;
                                            done = true;
                                        }
                                    }
                                }
                            }

                            //verify that all labels are found
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                if (!label_found[label_idx])
                                {
                                    add_message(true, "Label " + label_found[label_idx] + "is not found in worksheet " + tmpname);
                                    error_happened = true;
                                }
                            }

                            if (error_happened)
                            {
                                add_message(true, "skipping export of Crafting Count Level Adjust Table");
                            }
                            else
                            {
                                add_message(false, "Exported Crafting Count Level Adjust Table");

                                sw.WriteLine(spaces.Substring(0, indent) + "CraftingCountAdjust");
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;
                                sw.Write(spaces.Substring(0, indent) + "Level ");

                                for (int ii = 0; ii < NUM_LEVELS; ii++)
                                {
                                    int row = label_row[0] + 2 + ii;
                                    int col = label_col[0] + 2;
                                    String tmpS = dt.Rows[row][col].ToString();

                                    tmpS = tmpS.Trim();

                                    sw.Write(tmpS.ToString() + ", ");
                                }
                                sw.WriteLine("");

                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");

                                sw.WriteLine("");
                                sw.WriteLine("");
                            }
                        }
                        else if (tmpname == "algo item levels")
                        {
                            int NUM_LABELS = 1;
                            String[] label = { "algo item levels" };
                            int[] label_row = { -1, };
                            int[] label_col = { -1, };
                            bool[] label_found = { false, };
                            String[] quality_name = { "white", "yellow", "green", "blue", "purple" };

                            //scan sheet for labels
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                bool done = false;

                                for (int row = 0; row < dt.Rows.Count && !done; row++)
                                {
                                    for (int col = 0; col < dt.Columns.Count && !done; col++)
                                    {
                                        if (dt.Rows[row][col].ToString().ToLower().Trim() == label[label_idx])
                                        {
                                            label_row[label_idx] = row;
                                            label_col[label_idx] = col;
                                            label_found[label_idx] = true;
                                            done = true;
                                        }
                                    }
                                }
                            }

                            //verify that all labels are found
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                if (!label_found[label_idx])
                                {
                                    add_message(true, "Label " + label_found[label_idx] + "is not found in worksheet " + tmpname);
                                    error_happened = true;
                                }
                            }

                            if (error_happened)
                            {
                                add_message(true, "skipping export of Algo Item Level Tables");
                            }
                            else
                            {
                                add_message(false, "Exported Algo Item Level Tables");

                                for (int qual_idx = 0; qual_idx < quality_name.Length; qual_idx++)
                                {
                                    sw.WriteLine(spaces.Substring(0, indent) + "AlgoItemLevels " + quality_name[qual_idx]);
                                    sw.WriteLine(spaces.Substring(0, indent) + "{");
                                    indent += indentstep;
                                    sw.Write(spaces.Substring(0, indent) + "Level ");

                                    for (int ii = 0; ii < NUM_LEVELS; ii++)
                                    {
                                        int row = label_row[0] + 2 + ii;
                                        int col = label_col[0] + qual_idx;
                                        String tmpS = dt.Rows[row][col].ToString();

                                        tmpS = tmpS.Trim();
                                        int val = int.Parse(tmpS);

                                        sw.Write(val.ToString() + ", ");
                                    }
                                    sw.WriteLine("");

                                    indent -= indentstep;
                                    sw.WriteLine(spaces.Substring(0, indent) + "}");
                                }

                                sw.WriteLine("");
                                sw.WriteLine("");
                            }
                        }
                        else if (tmpname == "algo component counts")
                        {
                            int NUM_LABELS = 8;
                            String[] label = { "group1", "group2", "group3", "group4", "group5", "group6", "group7", "group8" };
                            int[] label_row = { -1, -1, -1, -1, -1, -1, -1, -1, };
                            int[] label_col = { -1, -1, -1, -1, -1, -1, -1, -1, };
                            bool[] label_found = { false, false, false, false, false, false, false, false, };

                            //scan sheet for labels
                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                bool done = false;

                                for (int row = 0; row < dt.Rows.Count && !done; row++)
                                {
                                    for (int col = 0; col < dt.Columns.Count && !done; col++)
                                    {
                                        if (dt.Rows[row][col].ToString().ToLower().Trim() == label[label_idx])
                                        {
                                            label_row[label_idx] = row;
                                            label_col[label_idx] = col;
                                            label_found[label_idx] = true;
                                            done = true;
                                        }
                                    }
                                }
                            }

                            for (int label_idx = 0; label_idx < NUM_LABELS; label_idx++)
                            {
                                if (!label_found[label_idx])
                                    continue;

                                add_message(false, "Exported " + label[label_idx] + " Component Count Tables");

                                sw.WriteLine(spaces.Substring(0, indent) + "ComponentCountTableEntry");
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;


                                //loop for all levels
                                for (int ii = 0; ii < NUM_LEVELS; ii++)
                                {
                                    sw.WriteLine(spaces.Substring(0, indent) + "ComponentCountLevelEntry");
                                    sw.WriteLine(spaces.Substring(0, indent) + "{");
                                    indent += indentstep;

                                    //loop for all qualities
                                    for (int jj = 0; jj < NUM_QUALITIES; jj++)
                                    {
                                        sw.WriteLine(spaces.Substring(0, indent) + "ComponentCountQualityEntry");
                                        sw.WriteLine(spaces.Substring(0, indent) + "{");
                                        indent += indentstep;


                                        //loop for all components
                                        for (int kk = 0; kk < NUM_COMPTYPES; kk++)
                                        {
                                            int row = label_row[label_idx] + 1 + ii;
                                            int col = label_col[label_idx] + 1 + (jj * NUM_COMPTYPES) + kk;
                                            String tmpS = dt.Rows[row][col].ToString();

                                            tmpS = tmpS.Trim();

                                            sw.WriteLine(spaces.Substring(0, indent) + "CompCountEntry " + tmpS);
                                        }
                                        
                                        indent -= indentstep;
                                        sw.WriteLine(spaces.Substring(0, indent) + "}");
                                    }

                                    indent -= indentstep;
                                    sw.WriteLine(spaces.Substring(0, indent) + "}");

                                    sw.WriteLine("");
                                }
                                sw.WriteLine("");

                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");

                                sw.WriteLine("");
                                sw.WriteLine("");
                            }
                        }
                    
                    
                    
                    }

                    indent -= indentstep;
                    sw.WriteLine(spaces.Substring(0, indent) + "}");

                    sw.Close();
                }
            }

            add_message(false, "Done");

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
            if (excel2007mode)
                excelReader.Close();
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