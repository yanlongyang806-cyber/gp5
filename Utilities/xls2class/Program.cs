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


namespace xls2class
{
    static class Program
    {
        [STAThread]
        static void Main()
        {
            String spaces = "                                                                                                                                            ";
            const int NUM_LEVELS = 60;
            const int indentstep = 4;


            object tmpObj = new object();

            String SourceFileName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2class", "sourcefile", "");
            if (tmpObj != null)
            {
                SourceFileName = tmpObj.ToString();
            }

            String ClassDestDirName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2class", "classdestdir", "");
            if (tmpObj != null)
            {
                ClassDestDirName = tmpObj.ToString();
            }

            String PowertableDestDirName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2class", "powertabledestdir", "");
            if (tmpObj != null)
            {
                PowertableDestDirName = tmpObj.ToString();
            }

            String PowerVarDestDirName = "";
            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2class", "powervardestdir", "");
            if (tmpObj != null)
            {
                PowerVarDestDirName = tmpObj.ToString();
            }

            //source excel file - SourceFileName
            //class file dest dir - ClassDestDirName
            //powertable file dest dir - PowertableDestDirName
            //powervar file dest dir - PowerVarDestDirName

            bool prompt_for_files = true;

            if ((SourceFileName != "") &&
                 (ClassDestDirName != "") &&
                 (PowertableDestDirName != "") &&
                 (PowerVarDestDirName != ""))
            {
                String msg = "";

                msg = "Excel File:          " + SourceFileName + "\n" +
                      "Class File Dir:      " + ClassDestDirName + "\n" +
                      "Powertable File Dir: " + PowertableDestDirName + "\n" +
                      "PowerVar File Dir:   " + PowerVarDestDirName + "\n\n" +
                      "Are these selections OK ?";

                if (MessageBox.Show(msg, "Selected Files/Dirs", MessageBoxButtons.YesNo) == DialogResult.Yes)
                {
                    prompt_for_files = false;
                }
            }


            if (prompt_for_files)
            {
                //if (SourceFileName == "")
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
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2class", "sourcefile", SourceFileName);
                }

                {
                    FolderBrowserDialog dlgdest = new FolderBrowserDialog();

                    dlgdest.SelectedPath = ClassDestDirName;
                    dlgdest.Description = "Select destination directory for Class files";

                    if (dlgdest.ShowDialog() != DialogResult.OK)
                    {
                        return;
                    }

                    ClassDestDirName = dlgdest.SelectedPath.ToString();
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2class", "classdestdir", ClassDestDirName);
                }


                {
                    FolderBrowserDialog dlgdest = new FolderBrowserDialog();

                    dlgdest.SelectedPath = PowertableDestDirName;
                    dlgdest.Description = "Select destination directory for Power Table files";

                    if (dlgdest.ShowDialog() != DialogResult.OK)
                    {
                        return;
                    }

                    PowertableDestDirName = dlgdest.SelectedPath.ToString();
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2class", "powertabledestdir", PowertableDestDirName);
                }


                {
                    FolderBrowserDialog dlgdest = new FolderBrowserDialog();

                    dlgdest.SelectedPath = PowerVarDestDirName;
                    dlgdest.Description = "Select destination directory for Power Var files";

                    if (dlgdest.ShowDialog() != DialogResult.OK)
                    {
                        return;
                    }

                    PowerVarDestDirName = dlgdest.SelectedPath.ToString();
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2class", "powervardestdir", PowerVarDestDirName);
                }
            }

            String ClassInfoFilename = ClassDestDirName + "\\..\\config\\" + "CharacterClassInfo.def";

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

                const int DISPLAYNAME = 0;
                const int DESCRIPTION = 1;
                const int DESCRIPTIONLONG = 2;
                const int ATTRIBUTE = 3;
                const int DAMAGETYPE = 4;
                const int DAMAGE_TABLES = 5;
                const int POWER_TABLES = 6;
                const int POWER_VARS = 7;
                const int KPM = 8;
                const int PLAYERCLASS = 9;
                const int ATTRIBCURVE = 10;
                const int COMBATMODS = 11;
                const int POWERS = 12;
                const int CLASSTYPE = 13;
                const int CLASSCATEGORY = 14;
                const int INVENTORYSET = 15;
                const int DEFAULTTRAY = 16;
                const int AUTOSPEND = 17;
                const int AUTOITEMDAMAGECHANCE = 18;
                const int AUTOITEMDAMAGEPROPORTION = 19;
                const int BASICFACTBONUSHITPOINTSMAXLEVEL = 20;
                const int POWERSLOTS = 21;
                const int POWERSTATS = 22;
                const int REQUIRES = 23;
                const int PERMITTEDSPECIES = 24;
                const int ICON = 25;
                const int PORTRAIT = 26;
                const int NEARDEATH = 27;
                const int ITEMART = 28;
                const int TACTICAL_ROLL = 29;
                const int TACTICAL_SPRINT = 30;
                const int TACTICAL_AIM = 31;
                const int STRAFING = 32;
                const int PROXIMITY_TARGET = 33;
                const int RETICLE = 34;
                const int COMBAT_REACTIVE_DEF = 35;
                const int POWER_STATES_DEF = 36;
                const int EXAMPLE_POWERS = 37;
                const int DEFAULT_STANCEWORD = 38;
                const int NUM_LABELS = 39;
                
                 
                String[] label = { "display name",
                                   "description",
                                   "descriptionlong",
                                   "attribute",
                                   "damage type",
                                   "damage tables",
                                   "powertables",
                                   "powervars",
                                   "kpm",
                                   "player class",
                                   "attribcurve",
                                   "combatmods",
                                   "powers",
                                   "class type",
                                   "class category",
                                   "inventoryset",
                                   "default tray",
                                   "autospend",
                                   "autoitemdamagechance",
                                   "autoitemdamageproportion",
                                   "basicfactbonushitpointsmaxlevel",
                                   "powerslots",
                                   "powerstats",
                                   "requirements",
                                   "permitted species",
                                   "icon",
                                   "portrait",
                                   "neardeath",
                                   "art",
                                   "tacticalroll",
                                   "tacticalsprint",
                                   "tacticalaim",
                                   "strafing",
                                   "proximity target",
                                   "reticle",
                                   "combat reactive power",
                                   "power states def",
                                   "example powers",
                                   "default stanceword"
                                    };

                int[] label_row = new int[NUM_LABELS];
                int[] label_col = new int[NUM_LABELS];
                bool[] label_found = new bool[NUM_LABELS];
                
                for(int ii=0; ii<NUM_LABELS; ii++)
                {
                    label_row[ii] = -1;
                    label_col[ii] = -1;
                    label_found[ii] = false;
                }
                
                
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
                for (int row = 0; row < dt.Rows.Count; row++)
                {
                    for (int col = 0; col < dt.Columns.Count; col++)
                    {
                        double parsedval = 0;
                        if (dt.Rows[row][col].GetType().Name == "String")
                            if (double.TryParse(dt.Rows[row][col].ToString(), out parsedval))
                            {
                                dt.Rows[row][col] = Math.Round(parsedval, 8);
                            }
                    }
                }



                int indent = 0;

                bool error_happened = false;


                String tmpname = dt.TableName.ToString().ToLower();

                if (tmpname.Contains("_powervars"))
                {
                    //tab is for a powervar list

                    String TableName = dt.TableName.ToString().Trim().Replace(" ", "");
                    TableName = TableName.Remove(TableName.Length - 10, 10);

                    String filename = PowerVarDestDirName + "\\" + TableName + ".powervar";

                    if (!label_found[POWER_VARS])
                    {
                        add_message(true, "Error: Missing POWERVARS in sheet " + dt.TableName.ToString());
                        error_happened = true;
                        add_message(true, "Errors in " + TableName + ". skipping export of " + filename);
                        continue;
                    }


                    if (File.Exists(filename) &&
                         ((File.GetAttributes(filename) & FileAttributes.ReadOnly) != 0))
                    {
                        add_message(true, filename + " is Read Only (not checked out), skipping export");
                        continue;
                    }

                    add_message(false,"Exported " + filename);

                    using (StreamWriter sw = new StreamWriter(filename))
                    {
                        if (label_row[POWER_VARS] != -1)
                        {
                            for (int row = label_row[POWER_VARS]; row < dt.Rows.Count; row++)
                            {
                                String var_name = dt.Rows[row][label_col[POWER_VARS] + 1].ToString().Trim();

                                if (var_name.StartsWith("_"))
                                    continue;

                                if (var_name.Length == 0)
                                    break;

                                sw.WriteLine(spaces.Substring(0, indent) + "PowerVar {0}", var_name);
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;
                                sw.Write(spaces.Substring(0, indent) + "Value FLT " + dt.Rows[row][label_col[POWER_VARS] + 2]);
                                sw.WriteLine("");
                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                            }
                        }

                        sw.Close();
                    }
                }

                else if (tmpname.Contains("_powertables"))
                {
                    //tab is for a powertable

                    String TableName = dt.TableName.ToString().Trim().Replace(" ", "");
                    TableName = TableName.Remove(TableName.Length - 12, 12);

                    String filename = PowertableDestDirName + "\\" + TableName + ".powertable";

                    if (!label_found[POWER_TABLES])
                    {
                        add_message(true, "Error: Missing POWER TABLE in sheet " + dt.TableName.ToString());
                        error_happened = true;
                        add_message(true, "Errors in " + TableName + ". skipping export of " + filename);
                        continue;
                    }
                        
                    if (File.Exists(filename) &&
                         ((File.GetAttributes(filename) & FileAttributes.ReadOnly) != 0))
                    {
                        add_message(true, filename + " is Read Only (not checked out), skipping export");
                        continue;
                    }

                    add_message(false, "Exported " + filename);

                    using (StreamWriter sw = new StreamWriter(filename))
                    {
                        if (label_row[POWER_TABLES] != -1)
                        {
                            for (int row = label_row[POWER_TABLES]; row < dt.Rows.Count; row++)
                            {
                                String table_name = dt.Rows[row][label_col[POWER_TABLES] + 1].ToString().Trim();

                                if (table_name.StartsWith("_"))
                                    continue;

                                if (table_name.Length == 0)
                                    break;

                                sw.WriteLine(spaces.Substring(0, indent) + "PowerTable {0}", table_name);
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;
                                sw.Write(spaces.Substring(0, indent) + "Values  ");
                                for (int ii = 0; (label_col[POWER_TABLES] + 2 + ii) < dt.Rows[row].Table.Columns.Count; ii++)
                                {
                                    String s = dt.Rows[row][label_col[POWER_TABLES] + 2 + ii].ToString();
                                    if (s.Length <= 0)
                                        break;
                                    sw.Write(s + ", ");
                                }
                                sw.WriteLine("");
                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                            }
                        }

                        sw.Close();
                    }
                }

                else if (tmpname.Contains("combatmods"))
                {
                    //tab is for a combatmods table

                    String filename = PowertableDestDirName + "\\..\\config\\" + "combatmods.def";

                    if (!label_found[COMBATMODS])
                    {
                        add_message(true, "Error: Missing CombatMods in sheet " + dt.TableName.ToString());
                        error_happened = true;
                        add_message(true, "Errors in " + dt.TableName.ToString() + ". skipping export of " + filename);
                        continue;
                    }

                    if (File.Exists(filename) &&
                         ((File.GetAttributes(filename) & FileAttributes.ReadOnly) != 0))
                    {
                        add_message(true, filename + " is Read Only (not checked out), skipping export");
                        continue;
                    }

                    add_message(false, "Exported " + filename);

                    using (StreamWriter sw = new StreamWriter(filename))
                    {
                        indent = 0;

                        for (int level = 0; level < NUM_LEVELS; level++)
                        {
                            bool group_active = false;

                            sw.WriteLine(spaces.Substring(0, indent) + "CombatMods // Level difference {0}", level);
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            for (int row = label_row[COMBATMODS]+1; row < dt.Rows.Count; row++)
                            {
                                String goup_name = dt.Rows[row][label_col[COMBATMODS]].ToString().Trim();
                                String member_name = dt.Rows[row][label_col[COMBATMODS] + 1].ToString().Trim();

                                if (goup_name.StartsWith("_"))
                                    continue;

                                if (member_name.StartsWith("_"))
                                    continue;

                                if ((goup_name.Length == 0) && (member_name.Length == 0))
                                {
                                    //at end of table

                                    if (group_active)
                                    {
                                        //end of active group
                                        indent -= indentstep;
                                        sw.WriteLine(spaces.Substring(0, indent) + "}");

                                        group_active = false;
                                    }
                                    break;
                                }

                                if (!group_active)
                                {
                                    if (goup_name.Length > 0)
                                    {
                                        //new group
                                        sw.WriteLine(spaces.Substring(0, indent) + "{0}", goup_name);
                                        sw.WriteLine(spaces.Substring(0, indent) + "{");
                                        indent += indentstep;

                                        group_active = true;
                                    }
                                }

                                else
                                {
                                    //in a group
                                    if (goup_name.Length > 0)
                                    {
                                        //end current group, start new group
                                        indent -= indentstep;
                                        sw.WriteLine(spaces.Substring(0, indent) + "}");

                                        sw.WriteLine(spaces.Substring(0, indent) + "{0}", goup_name);
                                        sw.WriteLine(spaces.Substring(0, indent) + "{");
                                        indent += indentstep;
                                    }

                                    else if (member_name.Length > 0)
                                    {
                                        sw.WriteLine(spaces.Substring(0, indent) + "{0} {1}", member_name, dt.Rows[row][label_col[COMBATMODS] + 2 + level].ToString());
                                    }
                                }
                            }

                            if (group_active)
                            {
                                //end of active group
                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");

                                group_active = false;
                            }

                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");
                        }

                        sw.Close();
                    }
                }

                else
                {
                    //tab is for a class

                    String TableName = dt.TableName.ToString().Trim().Replace(" ", "");

                    String filename = ClassDestDirName + "\\" + TableName + ".class";

                    String DisplayNameKey = "\"" + "CharacterClass." + TableName + ".DisplayName" + "\"";
                    String DisplayName = TableName + " displayname";

                    String DescriptionKey = "\"" + "CharacterClass." + TableName + ".Description" + "\"";
                    String Description = TableName + " description";

                    String DescriptionLongKey = "\"" + "CharacterClass." + TableName + ".DescriptionLong" + "\"";
                    String DescriptionLong = "";

                    if (label_found[DISPLAYNAME])
                    {
                        //add_message(false, "Error: Display Name found in sheet " + TableName);
                        DisplayName = dt.Rows[label_row[DISPLAYNAME]][label_col[DISPLAYNAME]+1].ToString().Trim();
                    }

                    if (label_found[DESCRIPTION])
                    {
                        //add_message(false, "Error: Description found in sheet " + TableName);
                        Description = dt.Rows[label_row[DESCRIPTION]][label_col[DESCRIPTION]+1].ToString().Trim();
                    }

                    if (label_found[DESCRIPTIONLONG])
                    {
                        //add_message(false, "Error: DescriptionLong found in sheet " + TableName);
                        DescriptionLong = dt.Rows[label_row[DESCRIPTIONLONG]][label_col[DESCRIPTIONLONG] + 1].ToString().Trim();
                    }

                    if (!label_found[ATTRIBUTE])
                    {
                        add_message(true, "Error: Missing Attribute in sheet " + dt.TableName.ToString());
                        error_happened = true;
                    }

                    /*
                    if (!label_found[DAMAGETYPE])
                    {
                        add_message(true, "Error: Missing DAMAGE TYPE in sheet " + dt.TableName.ToString());
                        error_happened = true;
                    }
                    */

                    //if (!label_found[DAMAGE_TABLES])
                    //if (!label_found[KPM])
                    //if (!label_found[PLAYERCLASS])
                    //if (!label_found[ATTRIBCURVE])
                    //if (!label_found[POWERS])
 

                    if (error_happened)
                    {
                        add_message(true, "Errors in " + TableName + ". skipping export of " + filename);
                        continue;
                    }

                    if ((!File.Exists(filename)) &&
                        (File.Exists(ClassInfoFilename)) &&
                        ((File.GetAttributes(ClassInfoFilename) & FileAttributes.ReadOnly) != 0))
                    {
                        //adding new class file but ClassInfo is not checked out
                        add_message(true, "ERROR: adding new class while CharacterClassInfo.def is not checked out.");
                    }
                    
             
                    if (File.Exists(filename) &&
                         ((File.GetAttributes(filename) & FileAttributes.ReadOnly) != 0))
                    {
                        add_message(true, filename + " is Read Only (not checked out), skipping export");
                        continue;
                    }

                    add_message(false, "Exported " + filename);

                    using (StreamWriter sw = new StreamWriter(filename))
                    {
                        sw.WriteLine(spaces.Substring(0, indent) + "CharacterClass {0}", TableName);
                        sw.WriteLine(spaces.Substring(0, indent) + "{");
                        indent += indentstep;

                        sw.WriteLine(spaces.Substring(0, indent) + "msgDisplayName  " + DisplayNameKey );
                        sw.WriteLine(spaces.Substring(0, indent) + "msgDescription  " + DescriptionKey);
                        if(DescriptionLong.Length > 0)
                            sw.WriteLine(spaces.Substring(0, indent) + "msgDescriptionLong  " + DescriptionLongKey);

                        const int MAX_ASPECTS = 8;
                        String[] aspects = new String[MAX_ASPECTS];
                        int num_aspects = 0;

                        int start_row = label_row[ATTRIBUTE] + 1;
                        int attrname_col = label_col[ATTRIBUTE];
                        int aspect_col = label_col[ATTRIBUTE] + 1;

                        if ( label_found[CLASSTYPE] )
                        {
                            String tmptypestring = dt.Rows[label_row[CLASSTYPE]][label_col[CLASSTYPE] + 1].ToString().Trim();
                            sw.WriteLine(spaces.Substring(0, indent) + "Type  " + tmptypestring);
                        }

                        if (label_found[CLASSCATEGORY])
                        {
                            String tmptypestring = dt.Rows[label_row[CLASSCATEGORY]][label_col[CLASSCATEGORY] + 1].ToString().Trim();
                            if (tmptypestring.Length > 0)
                            {
                                sw.WriteLine(spaces.Substring(0, indent) + "Category  " + tmptypestring);
                            }
                        }

                        if (label_found[INVENTORYSET])
                        {
                            String tmptypestring = dt.Rows[label_row[INVENTORYSET]][label_col[INVENTORYSET] + 1].ToString().Trim();
                            sw.WriteLine(spaces.Substring(0, indent) + "InventorySet  " + tmptypestring);
                        }

                        if (label_found[DEFAULTTRAY])
                        {
                            String tmptypestring = dt.Rows[label_row[DEFAULTTRAY]][label_col[DEFAULTTRAY] + 1].ToString().Trim();
                            if (tmptypestring.Length > 0)
                            {
                                sw.WriteLine(spaces.Substring(0, indent) + "DefaultTray  " + tmptypestring);
                            }
                        }

                        if (label_found[AUTOSPEND])
                        {
                            String tmptypestring = dt.Rows[label_row[AUTOSPEND]][label_col[AUTOSPEND] + 1].ToString().Trim();
                            String tmptypestring2 = dt.Rows[label_row[AUTOSPEND]][label_col[AUTOSPEND] + 2].ToString().Trim();
                            int i = 0;

                            while ((tmptypestring.Length > 0) && (tmptypestring2.Length > 0))
                            {
                                ++i;
                                sw.WriteLine(spaces.Substring(0, indent) + "AutoSpend");
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "Type Stat" + tmptypestring);
                                sw.WriteLine(spaces.Substring(0, indent) + "Points " + tmptypestring2);
                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");

                                tmptypestring = dt.Rows[label_row[AUTOSPEND]+i][label_col[AUTOSPEND] + 1].ToString().Trim();
                                tmptypestring2 = dt.Rows[label_row[AUTOSPEND]+i][label_col[AUTOSPEND] + 2].ToString().Trim();
                            }
                        }
                        if (label_found[AUTOITEMDAMAGECHANCE])
                        {
                            String tmptypestring = dt.Rows[label_row[AUTOITEMDAMAGECHANCE]][label_col[AUTOITEMDAMAGECHANCE] + 1].ToString().Trim();
                            sw.WriteLine(spaces.Substring(0, indent) + "AutoItemDamageChance " + tmptypestring);
                        }
                        if (label_found[AUTOITEMDAMAGEPROPORTION])
                        {
                            String tmptypestring = dt.Rows[label_row[AUTOITEMDAMAGEPROPORTION]][label_col[AUTOITEMDAMAGEPROPORTION] + 1].ToString().Trim();
                            sw.WriteLine(spaces.Substring(0, indent) + "AutoItemDamageProportion " + tmptypestring);
                        }
                        if (label_found[BASICFACTBONUSHITPOINTSMAXLEVEL])
                        {
                            String tmptypestring = dt.Rows[label_row[BASICFACTBONUSHITPOINTSMAXLEVEL]][label_col[BASICFACTBONUSHITPOINTSMAXLEVEL] + 1].ToString().Trim();
                            sw.WriteLine(spaces.Substring(0, indent) + "BasicFactBonusHitPointsMaxLevel " + tmptypestring);
                        }
                        sw.WriteLine("");

                        //scan table to get a list of all unique aspects
                        for (int row = start_row; row < dt.Rows.Count; row++)
                        {
                            String tmpattrname = dt.Rows[row][attrname_col].ToString().Trim();
                            String tmpaspect = dt.Rows[row][aspect_col].ToString().Trim();

                            //first blank attr and aspect ends the table
                            if (tmpattrname.Length == 0 && tmpaspect.Length == 0)
                                break;

                            if (tmpaspect.Length == 0 || tmpaspect.StartsWith("_"))
                                continue;

                            int ii;
                            for (ii = 0; ii < num_aspects; ii++)
                            {
                                if (tmpaspect == aspects[ii])
                                    break;
                            }
                            if (ii >= num_aspects)
                            {
                                aspects[num_aspects] = tmpaspect;
                                num_aspects++;
                            }
                        }


                        //scan damagetype to figure out what aspects are there
                        //const int NUM_DAMAGE_TYPES = 30;
                        int[] damagetype_aspect_row = new int[MAX_ASPECTS];
                        for (int aspect_idx = 0; aspect_idx < num_aspects; aspect_idx++)
                        {
                            String aspect_name = "";
                            damagetype_aspect_row[aspect_idx] = -1;

                            for (int row = label_row[DAMAGETYPE]; row < dt.Rows.Count; row++)
                            {
                                aspect_name = dt.Rows[row][label_col[DAMAGETYPE] + 1].ToString();

                                if (aspect_name.StartsWith("_"))
                                    continue;

                                if (aspect_name.Length == 0)
                                    break;

                                if (aspect_name == aspects[aspect_idx])
                                {
                                    damagetype_aspect_row[aspect_idx] = row;
                                    break;
                                }
                            }
                        }


                        //loop through and output data for all apsects
                        for (int aspect_idx = 0; aspect_idx < num_aspects; aspect_idx++)
                        {
                            String attr_name = "";

                            //loop for all levels
                            for (int level = 0; level < NUM_LEVELS; level++)
                            {
                                int level_col = label_col[ATTRIBUTE] + 2 + level;
                                int foundlevel = 0;

                                // Check if we actually have anything specified at this level
                                for (int row = start_row; row < dt.Rows.Count; row++)
                                {
                                    String tmpattrname = dt.Rows[row][attrname_col].ToString().Trim();
                                    String tmpaspect = dt.Rows[row][aspect_col].ToString().Trim();

                                    //first blank attr and aspect ends the table
                                    if (tmpattrname.Length == 0 && tmpaspect.Length == 0)
                                        break;

                                    if (level_col < dt.Rows[row].Table.Columns.Count)
                                    {
                                        foundlevel = 1;
                                        break;
                                    }
                                }

                                // If not, break the level loop
                                if (foundlevel == 0)
                                    break;

                                sw.WriteLine(spaces.Substring(0, indent) + "Attr" + aspects[aspect_idx] + "    //" + (level + 1).ToString());
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;

                                /*
                                if (damagetype_aspect_row[aspect_idx] != -1)
                                {
                                    sw.Write(spaces.Substring(0, indent) + "afDamageType  ");
                                    for (int ii = 0; ii < NUM_DAMAGE_TYPES; ii++)
                                        sw.Write(dt.Rows[damagetype_aspect_row[aspect_idx]][label_col[DAMAGETYPE] + 2 + level] + ", ");
                                    sw.WriteLine("");
                                }
                                */

                                for (int row = start_row; row < dt.Rows.Count; row++)
                                {
                                    String tmpattrname = dt.Rows[row][attrname_col].ToString().Trim();
                                    String tmpaspect = dt.Rows[row][aspect_col].ToString().Trim();

                                    //first blank attr and aspect ends the table
                                    if (tmpattrname.Length == 0 && tmpaspect.Length == 0)
                                        break;

                                    if (level_col >= dt.Rows[row].Table.Columns.Count)
                                        continue;

                                    if (tmpattrname.Length > 0)
                                        attr_name = tmpattrname;

                                    if(attr_name.IndexOf('(') >=0 && attr_name.EndsWith(")"))
                                    {
                                        int indexStart = attr_name.IndexOf('(') + 1;
                                        attr_name = attr_name.Substring(indexStart,(attr_name.Length-indexStart)-1);
                                    }

                                    if (tmpaspect.Length == 0 || tmpaspect.StartsWith("_"))
                                        continue;

                                    if (tmpaspect == aspects[aspect_idx])
                                    {
                                        String val = dt.Rows[row][level_col].ToString().Trim();

                                        sw.WriteLine(spaces.Substring(0, indent) + "f" + attr_name + "  " + val);
                                    }
                                }

                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                            }
                        }
                        sw.WriteLine("");



                        if (label_row[DAMAGE_TABLES] != -1)
                        {
                            for (int row = label_row[DAMAGE_TABLES]; row < dt.Rows.Count; row++)
                            {
                                String table_name = dt.Rows[row][label_col[DAMAGE_TABLES] + 1].ToString().Trim();

                                if (table_name.StartsWith("_"))
                                    continue;

                                if (table_name.Length == 0)
                                    break;

                                sw.WriteLine(spaces.Substring(0, indent) + "PowerTable {0}", table_name);
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;
                                sw.Write(spaces.Substring(0, indent) + "Values  ");
                                for (int ii = 0; ii < NUM_LEVELS && ((label_col[DAMAGE_TABLES] + 2 + ii) < dt.Rows[row].Table.Columns.Count); ii++)
                                    sw.Write(dt.Rows[row][label_col[DAMAGE_TABLES] + 2 + ii].ToString() + ", ");
                                sw.WriteLine("");
                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                            }
                        }

                        if (label_row[KPM] != -1)
                        {
                            sw.Write(spaces.Substring(0, indent) + "exprKPM  " + dt.Rows[label_row[KPM]][label_col[KPM] + 1].ToString());
                            sw.WriteLine("");
                            sw.WriteLine("");
                        }

                        if (label_row[PLAYERCLASS] != -1)
                        {
                            sw.Write(spaces.Substring(0, indent) + "PlayerClass  " + dt.Rows[label_row[PLAYERCLASS]][label_col[PLAYERCLASS] + 1].ToString());
                            sw.WriteLine("");
                            sw.WriteLine("");
                        }

                        if (label_row[REQUIRES] != -1)
                        {
                            int expr_col = label_col[REQUIRES] + 1;
                            int row = label_row[REQUIRES];

                            sw.WriteLine(spaces.Substring(0, indent) + "exprRequiresBlock");
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            sw.WriteLine(spaces.Substring(0, indent) + "Statement <& " + dt.Rows[row][expr_col].ToString().Trim() + " &>");

                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");
                        }

                        if (label_row[NEARDEATH] != -1)
                        {
                            int row = label_row[NEARDEATH];
                            int chance_col = label_col[NEARDEATH] + 1;
                            int time_col = label_col[NEARDEATH] + 2;
                            int bits_col = label_col[NEARDEATH] + 3;
                            int stances_col = label_col[NEARDEATH] + 4;
                            int powermode_col = label_col[NEARDEATH] + 5;
                            int attribDyingTime_col = label_col[NEARDEATH] + 6;

                            sw.WriteLine(spaces.Substring(0, indent) + "NearDeathConfig");
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            if (dt.Rows[row][chance_col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "Chance " + dt.Rows[row][chance_col].ToString().Trim());

                            if (dt.Rows[row][time_col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "Time " + dt.Rows[row][time_col].ToString().Trim());

                            if (dt.Rows[row][bits_col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "Bits " + dt.Rows[row][bits_col].ToString().Trim());

                            if (dt.Rows[row][stances_col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "StanceWords " + dt.Rows[row][stances_col].ToString().Trim());

                            if (dt.Rows[row][powermode_col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "PowerModeRequired " + dt.Rows[row][powermode_col].ToString().Trim());

                            if (dt.Rows[row][attribDyingTime_col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "DyingTimeAttrib " + dt.Rows[row][attribDyingTime_col].ToString().Trim());
                                                        

                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");
                        }

                        if (label_row[ATTRIBCURVE] != -1)
                        {
                            int ac_start_row = label_row[ATTRIBCURVE] + 1;
                            int ac_attrname_col = label_col[ATTRIBCURVE];
                            int ac_aspect_col = label_col[ATTRIBCURVE] + 1;
                            int ac_type_col = label_col[ATTRIBCURVE] + 2;
                            int ac_val_col = label_col[ATTRIBCURVE] + 3;

                            String tmpattrname = "";
                            String tmpaspect = "";
                            String tmptype = "";

                            //scan table to get a list of all unique aspects
                            for (int row = ac_start_row; row < dt.Rows.Count; row++)
                            {
                                //first blank attr and aspect ends the table
                                if ((dt.Rows[row][ac_attrname_col].ToString().Trim().Length == 0) &&
                                     (dt.Rows[row][ac_aspect_col].ToString().Trim().Length == 0))
                                    break;

                                if (dt.Rows[row][ac_attrname_col].ToString().Trim().Length > 0)
                                    tmpattrname = dt.Rows[row][ac_attrname_col].ToString().Trim();
                                tmpaspect = dt.Rows[row][ac_aspect_col].ToString().Trim();
                                tmptype = dt.Rows[row][ac_type_col].ToString().ToLower().Trim();

                                if (tmpaspect.Length == 0 || tmpaspect.StartsWith("_"))
                                    continue;

                                switch (tmptype)
                                {
                                    case "max":
                                        sw.WriteLine(spaces.Substring(0, indent) + "AttribCurve AttribCurveMax");
                                        sw.WriteLine(spaces.Substring(0, indent) + "{");
                                        indent += indentstep;

                                        sw.WriteLine(spaces.Substring(0, indent) + "Attrib " + tmpattrname);
                                        sw.WriteLine(spaces.Substring(0, indent) + "Aspect " + tmpaspect);

                                        sw.WriteLine(spaces.Substring(0, indent) + "Max " + dt.Rows[row][ac_val_col].ToString());

                                        indent -= indentstep;
                                        sw.WriteLine(spaces.Substring(0, indent) + "}");
                                        sw.WriteLine("");
                                        break;

                                    case "quadraticmax":
                                        sw.WriteLine(spaces.Substring(0, indent) + "AttribCurve AttribCurveQuadraticMax");
                                        sw.WriteLine(spaces.Substring(0, indent) + "{");
                                        indent += indentstep;

                                        sw.WriteLine(spaces.Substring(0, indent) + "Attrib " + tmpattrname);
                                        sw.WriteLine(spaces.Substring(0, indent) + "Aspect " + tmpaspect);

                                        sw.WriteLine(spaces.Substring(0, indent) + "Max " + dt.Rows[row][ac_val_col].ToString());

                                        indent -= indentstep;
                                        sw.WriteLine(spaces.Substring(0, indent) + "}");
                                        sw.WriteLine("");
                                        break;

                                    default:
                                        break;
                                }
                            }
                        }


                        if (label_row[POWERS] != -1)
                        {
                            int powers_row = label_row[POWERS] + 1;
                            int powers_col = label_col[POWERS];

                            String powername = "";

                            //scan table to get a list of all unique aspects
                            for (int row = powers_row; row < dt.Rows.Count; row++)
                            {
                                //first blank ends the table
                                if (dt.Rows[row][powers_col].ToString().Trim().Length == 0)
                                    break;

                                powername = dt.Rows[row][powers_col].ToString().Trim();


                                sw.WriteLine(spaces.Substring(0, indent) + "Power");
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;

                                sw.WriteLine(spaces.Substring(0, indent) + "Def " + powername);
                                
                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                            }
                        }

                        if (label_found[POWERSLOTS])
                        {
                            int ii;

                            for(ii=0;;ii++)
                            {
                                int row = label_row[POWERSLOTS] + 1 + ii;
                                int type_col = label_col[POWERSLOTS] + 1;
                                int requires_col = label_col[POWERSLOTS] + 2;
                                int excludes_col = label_col[POWERSLOTS] + 3;
                                int defreplace_col = label_col[POWERSLOTS] + 4;

                                if ((dt.Rows[row][type_col].ToString().Trim().Length == 0) &&
                                     (dt.Rows[row][requires_col].ToString().Trim().Length == 0) &&
                                     (dt.Rows[row][excludes_col].ToString().Trim().Length == 0) &&
                                     (dt.Rows[row][defreplace_col].ToString().Trim().Length == 0))
                                    break;

                                sw.WriteLine(spaces.Substring(0, indent) + "PowerSlot");
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;

                                if (dt.Rows[row][type_col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "Type " + dt.Rows[row][type_col].ToString().Trim());

                                if (dt.Rows[row][requires_col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "Requires " + dt.Rows[row][requires_col].ToString().Trim());

                                if (dt.Rows[row][excludes_col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "Excludes " + dt.Rows[row][excludes_col].ToString().Trim());

                                if (dt.Rows[row][defreplace_col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "DefAutoSlotReplace " + dt.Rows[row][defreplace_col].ToString().Trim());

                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                            }
                        }


                        if (label_found[POWERSTATS])
                        {
                            int ii;

                            for (ii = 0; ; ii++)
                            {
                                int row = label_row[POWERSTATS] + 1 + ii;
                                int col = label_col[POWERSTATS] + 1;

                                if (dt.Rows[row][col].ToString().Trim().Length == 0) 
                                    break;

                                //Name
                                sw.WriteLine(spaces.Substring(0, indent) + "PowerStat " + dt.Rows[row][col].ToString().Trim());
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;

                                col++;//Stat
                                if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "Stat " + dt.Rows[row][col].ToString().Trim());
                                
                                col++;//Attrib
                                if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "Attrib " + dt.Rows[row][col].ToString().Trim());

                                col++;//Aspect
                                if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "Aspect " + dt.Rows[row][col].ToString().Trim());

                                col++;//Expr
                                if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "Expr <<" + dt.Rows[row][col].ToString().Trim() + ">>");

                                col++;//PowerTreenodeRequired
                                if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "PowerTreeNodeRequired " + dt.Rows[row][col].ToString().Trim());

                                col++;//PowerDefRequired
                                if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                    sw.WriteLine(spaces.Substring(0, indent) + "PowerDefRequired " + dt.Rows[row][col].ToString().Trim());

                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                            }
                        }

                        if (label_found[PERMITTEDSPECIES])
                        {
                            int ii;
                            for (ii = 0; ; ii++)
                            {
                                int row = label_row[PERMITTEDSPECIES] + 1 + ii;
                                int col = label_col[PERMITTEDSPECIES];

                                if (dt.Rows[row][col].ToString().Trim().Length == 0)
                                    break;

                                // Species name
                                sw.WriteLine(spaces.Substring(0, indent) + "PermittedSpecies " + dt.Rows[row][col].ToString().Trim());
                            }

                            sw.WriteLine("");
                        }

                        if (label_found[ICON])
                        {
                            sw.WriteLine(spaces.Substring(0, indent) + "IconName " + dt.Rows[label_row[ICON]][label_col[ICON] + 1]);
                            sw.WriteLine("");
                        }

                        if (label_found[PORTRAIT])
                        {
                            sw.WriteLine(spaces.Substring(0, indent) + "PortraitName " + dt.Rows[label_row[PORTRAIT]][label_col[PORTRAIT] + 1]);
                            sw.WriteLine("");
                        }


                        if (label_found[ITEMART])
                        {
                            int row = label_row[ITEMART];
                            int art_col = label_col[ITEMART] + 1;
                            int fx_col = label_col[ITEMART] + 2;

                            if (dt.Rows[row][art_col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "Art " + dt.Rows[row][art_col]);

                            if (dt.Rows[row][fx_col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "Fx " + dt.Rows[row][fx_col]);
                            
                            sw.WriteLine("");
                        }

                        if (label_found[TACTICAL_ROLL])
                        {
                            int row = label_row[TACTICAL_ROLL];
                            int col = label_col[TACTICAL_ROLL] + 1;

                            
                            sw.WriteLine(spaces.Substring(0, indent) + "TacticalRollDef");
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollDisabled " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollDistance " + dt.Rows[row][col]);
                            
                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollSpeed " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollFrameStart " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollAccelNumberOfFrames " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollDecelNumberOfFrames " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollPostHoldSeconds " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollCooldown " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollFuelCost " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollFacesInRollDirection " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RollDisableDuringRootAttrib " + dt.Rows[row][col]);
                            
                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");
                        }


                        if (label_found[TACTICAL_SPRINT])
                        {
                            int row = label_row[TACTICAL_SPRINT];
                            int col = label_col[TACTICAL_SPRINT] + 1;

                            sw.WriteLine(spaces.Substring(0, indent) + "TacticalSprintDef");
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "SprintDisabled " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RunMaxDurationSeconds " + dt.Rows[row][col]);
                            
                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RunMaxDurationSecondsCombat " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RunCooldown " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RunCooldownCombat " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RunFuelRefillRate " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "RunFuelDelay " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "SpeedScaleSprint " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "SpeedScaleSprintCombat " + dt.Rows[row][col]);
                            
                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");
                        }

                        if (label_found[TACTICAL_AIM])
                        {
                            int row = label_row[TACTICAL_AIM];
                            int col = label_col[TACTICAL_AIM] + 1;

                            sw.WriteLine(spaces.Substring(0, indent) + "TacticalAimDef");
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "AimDisabled " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "SpeedScaleCrouch " + dt.Rows[row][col]);
                            
                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "AimMinDurationSeconds " + dt.Rows[row][col]);

                            col++;
                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "AimCooldown " + dt.Rows[row][col]);

                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");
                        }

                        if (label_found[STRAFING])
                        {
                            int row = label_row[STRAFING];
                            int col = label_col[STRAFING] + 1;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "Strafing " + dt.Rows[row][col].ToString().Trim());
                        }

                        if (label_found[PROXIMITY_TARGET])
                        {
                            int row = label_row[PROXIMITY_TARGET];
                            int col = label_col[PROXIMITY_TARGET] + 1;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "UseProximityTargetingAssistEnt " + dt.Rows[row][col].ToString().Trim());
                        }

                        if (label_found[RETICLE])
                        {
                            int row = label_row[RETICLE];
                            int col = label_col[RETICLE] + 1;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "ReticleDef " + dt.Rows[row][col].ToString().Trim());
                        }

                        if (label_found[COMBAT_REACTIVE_DEF])
                        {
                            int row = label_row[COMBAT_REACTIVE_DEF];
                            int col = label_col[COMBAT_REACTIVE_DEF] + 1;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "CombatReactivePowerDef " + dt.Rows[row][col].ToString().Trim());
                        }

                        if (label_found[POWER_STATES_DEF])
                        {
                            int row = label_row[POWER_STATES_DEF];
                            int col = label_col[POWER_STATES_DEF] + 1;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "CombatPowerStateSwitchingDef " + dt.Rows[row][col].ToString().Trim());
                        }

                        if (label_found[EXAMPLE_POWERS])
                        {
                            int row = label_row[EXAMPLE_POWERS];
                            int col = label_col[EXAMPLE_POWERS] + 1;

                            while (dt.Rows[row][col].ToString().Trim().Length > 0)
                            {
                                sw.WriteLine(spaces.Substring(0, indent) + "ExamplePower");
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "Def " + dt.Rows[row][col].ToString().Trim());
                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                                col++;
                            }

                        }

                        if (label_found[DEFAULT_STANCEWORD])
                        {
                            int row = label_row[DEFAULT_STANCEWORD];
                            int col = label_col[DEFAULT_STANCEWORD] + 1;

                            if (dt.Rows[row][col].ToString().Trim().Length > 0)
                                sw.WriteLine(spaces.Substring(0, indent) + "StanceWords " + dt.Rows[row][col].ToString().Trim());
                        }

                        indent -= indentstep;
                        sw.WriteLine(spaces.Substring(0, indent) + "}");
                        sw.WriteLine("");

                        sw.Close();
                    }


                    //if ( !File.Exists(filename + ".ms") )
                    {
                        using (StreamWriter sw = new StreamWriter(filename + ".ms"))
                        {
                            sw.WriteLine(spaces.Substring(0, indent) + "Message");
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            sw.WriteLine(spaces.Substring(0, indent) + "MessageKey " + DisplayNameKey);
                            sw.WriteLine(spaces.Substring(0, indent) + "Scope \"CharacterClass/" + TableName +"\"");
                            sw.WriteLine(spaces.Substring(0, indent) + "Description \"CharacterClass Display Name\"");
                            sw.WriteLine(spaces.Substring(0, indent) + "DefaultString " + "\"" + DisplayName + "\"");

                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");


                            sw.WriteLine(spaces.Substring(0, indent) + "Message");
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            sw.WriteLine(spaces.Substring(0, indent) + "MessageKey " + DescriptionKey);
                            sw.WriteLine(spaces.Substring(0, indent) + "Scope \"CharacterClass/" + TableName + "\"");
                            sw.WriteLine(spaces.Substring(0, indent) + "Description \"CharacterClass Description\"");
                            sw.WriteLine(spaces.Substring(0, indent) + "DefaultString " + "\"" + Description + "\"");

                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");

                            if(DescriptionLong.Length > 0)
                            {
                                sw.WriteLine(spaces.Substring(0, indent) + "Message");
                                sw.WriteLine(spaces.Substring(0, indent) + "{");
                                indent += indentstep;

                                sw.WriteLine(spaces.Substring(0, indent) + "MessageKey " + DescriptionLongKey);
                                sw.WriteLine(spaces.Substring(0, indent) + "Scope \"CharacterClass/" + TableName + "\"");
                                sw.WriteLine(spaces.Substring(0, indent) + "Description \"CharacterClass Long Description\"");
                                sw.WriteLine(spaces.Substring(0, indent) + "DefaultString " + "\"" + DescriptionLong + "\"");

                                indent -= indentstep;
                                sw.WriteLine(spaces.Substring(0, indent) + "}");
                                sw.WriteLine("");
                            }

                            sw.Close();
                        }
                    }
                }
            }

            // changed from 256 to 1000. STO hit 256 on 1-23-2013
            const int MAX_CLASSES = 1000;
            String[] classes = new String[MAX_CLASSES];
            int num_classes = 0;

            const int MAX_BASE_CLASSES = MAX_CLASSES;
            String[] base_classes = new String[MAX_BASE_CLASSES];
            int num_base_classes = 0;

            //loop for all worksheets
            //get a list of all the class name, and determine all of the base class names.
            foreach (DataTable dt in theTables)
            {
                if (dt.TableName.ToString().StartsWith("_"))
                    continue;

                String tmpname = dt.TableName.ToString().ToLower().Trim();

                if (tmpname.Contains("_powervars"))
                {
                }

                else if (tmpname.Contains("_powertables"))
                {
                }

                else if (tmpname.Contains("combatmods"))
                {
                }

                else
                {
                    if (num_classes >= MAX_CLASSES)
                    {
                        add_message(false, "Error: MAX CLASSES hit " + MAX_CLASSES.ToString());
                        continue;
                    }
                    classes[num_classes] = tmpname;
                    num_classes++;

                    //tab is for a class
                    if ( ( !tmpname.StartsWith("object") ) &&
                         ( (tmpname.EndsWith("_1p")) ||
                           (tmpname.EndsWith("_2p")) ||
                           (tmpname.EndsWith("_3p")) ||
                           (tmpname.EndsWith("_4p")) ||
                           (tmpname.EndsWith("_5p")) ||
                           (tmpname.EndsWith("_weak")) ||
                           (tmpname.EndsWith("_tough")) )
                       )
                    {
                        //not a base class name
                    }
                    else
                    {
                        String tmpclassname = tmpname;
                        
                        if ( !tmpname.StartsWith("object") )
                            tmpclassname = tmpclassname.Replace("_default", "");

                        base_classes[num_base_classes] = tmpclassname;
                        num_base_classes++;
                    }
                }
            }

            {
                int indent = 0;

                if (File.Exists(ClassInfoFilename) &&
                    ((File.GetAttributes(ClassInfoFilename) & FileAttributes.ReadOnly) != 0))
                {
                    add_message(true, ClassInfoFilename + " is Read Only (not checked out), skipping export");
                }
                else
                {
                    add_message(false, "Exported " + ClassInfoFilename);

                    using (StreamWriter sw = new StreamWriter(ClassInfoFilename))
                    {
                        sw.WriteLine("// Character Class Names");

                        //loop for all base classes
                        for (int i = 0; i < num_base_classes; i++)
                        {
                            String tmpclassname = base_classes[i];

                            if (tmpclassname.Length == 0)
                                continue;

                            sw.WriteLine("// {0}", tmpclassname);
                        }
                        sw.WriteLine("");


                        //loop for all base classes
                        for (int i = 0; i < num_base_classes; i++)
                        {
                            String baseclassname = base_classes[i];

                            if (baseclassname.Length == 0)
                                continue;

                            sw.WriteLine(spaces.Substring(0, indent) + "CharacterClassInfo {0}", baseclassname);
                            sw.WriteLine(spaces.Substring(0, indent) + "{");
                            indent += indentstep;

                            sw.Write(spaces.Substring(0, indent) + "Normal ");

                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 1, "normal")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 2, "normal")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 3, "normal")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 4, "normal")); //1p
                            sw.Write("{0} ", GetClassName(baseclassname, classes, 5, "normal")); //1p
                            sw.WriteLine("");

                            sw.Write(spaces.Substring(0, indent) + "Weak   ");
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 1, "weak")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 2, "weak")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 3, "weak")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 4, "weak")); //1p
                            sw.Write("{0} ", GetClassName(baseclassname, classes, 5, "weak")); //1p
                            sw.WriteLine("");

                            sw.Write(spaces.Substring(0, indent) + "Tough  ");
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 1, "tough")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 2, "tough")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 3, "tough")); //1p
                            sw.Write("{0}, ", GetClassName(baseclassname, classes, 4, "tough")); //1p
                            sw.Write("{0} ", GetClassName(baseclassname, classes, 5, "tough")); //1p
                            sw.WriteLine("");

                            indent -= indentstep;
                            sw.WriteLine(spaces.Substring(0, indent) + "}");
                            sw.WriteLine("");
                        }
                        sw.WriteLine("");

                        sw.Close();
                    }
                }
            }

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

        static bool StringInArray(String name, String[] stringarray)
        {
            for (int ii = 0; ii < stringarray.Length; ii++)
            {
                if (name == stringarray[ii])
                    return true;
            }
            return false;
        }

        static String GetClassName(String name, String[] stringarray, int num_players, String toughness)
        {
            String tmpname = "";
            String[] toughnesses = { "", "_default", "_normal" };


            if (name.StartsWith("object"))
                return name;


            for (int ii = num_players; ii >= 1; ii--)
            {
                //check for most specific name variation first

                //normal toughness can end with "" or "_default" or "_normal"
                if (toughness == "normal")
                {
                    foreach (String t in toughnesses)
                    {
                        tmpname = name + "_" + ii.ToString() + "p" + t;
                        if (StringInArray(tmpname, stringarray))
                        {
                            return tmpname;
                        }
                    }
                }
                else
                {
                    tmpname = name + "_" + ii.ToString() + "p" + "_" + toughness;
                    if (StringInArray(tmpname, stringarray))
                    {
                        return tmpname;
                    }
                }


                //1 player can have "_1p" or not
                if (ii == 1)
                {
                    //normal toughness can end with "" or "_default" or "_normal"
                    if (toughness == "normal")
                    {
                        foreach (String t in toughnesses)
                        {
                            tmpname = name + t;
                            if (StringInArray(tmpname, stringarray))
                            {
                                return tmpname;
                            }
                        }
                    }
                    else
                    {
                        tmpname = name + "_" + toughness;
                        if (StringInArray(tmpname, stringarray))
                        {
                            return tmpname;
                        }
                    }
                }

            }

            if (toughness != "normal")
                tmpname = GetClassName(name, stringarray, num_players, "normal");

            return tmpname;
        }
    }
}