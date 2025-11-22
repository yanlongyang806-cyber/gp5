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

namespace xls2items
{
	public class Program
    {
		// enumeration of columns
		enum Columns {	NAME,
						SCOPE,
						BAG,
						SLOT,
						LEVEL,
						QUALITY,
						DURABILITY,
						REQ_SKILL_TYPE,
						REQ_SKILL_LEVEL,
						MIN_LEVEL,
						MAX_LEVEL,
						STR,
						DEX,
						CON,
						INT,
						EGO,
						PRE,
						REC,
						END,
						STR2,
						DEX2,
						CON2,
						INT2,
						EGO2,
						PRE2,
						REC2,
						END2 };

		// table of column labels
		static String[] labels = {	"name",
									"scope",
									"bag",
									"slot",
									"level",
									"quality",
									"durability",
									"req skill type",
									"req skill level",
									"min level",
									"max level",
									"str",
									"dex",
									"con",
									"int",
									"ego",
									"pre",
									"rec",
									"end",
									"str2",
									"dex2",
									"con2",
									"int2",
									"ego2",
									"pre2",
									"rec2",
									"end2"};

		// members
		static Form1 messages = new Form1();
		static bool errorFlag = false;

		// helper functions
		static void addMessage(bool error, String msg)
		{
			if (error)
				errorFlag = true;

			messages.listBox1.Items.Insert(messages.listBox1.Items.Count, msg);
			messages.Show();
			messages.Update();
		}

		static uint stringToUInt(String str)
		{
			try
			{
				return Convert.ToUInt32(str);
			}
			catch
			{
				return 0;
			}
		}

		static float stringToFloat(String str)
		{
			try
			{
				return Convert.ToSingle(str);
			}
			catch
			{
				return 0;
			}
		}

		static void streamWriteStatPower(StreamWriter sw, String itemPowerDef, String scaleStr)
		{
			float scale;

			// convert the string
			try
			{
				scale = Convert.ToSingle(scaleStr);
			}
			catch
			{
				scale = 0;
			}

			// write the item power out
			if (scale > 0)
			{
				sw.WriteLine();
				sw.WriteLine("\tItempowerdefrefs");
				sw.WriteLine("\t{");

				sw.WriteLine("\t\tItempowerdef " + itemPowerDef);
				sw.WriteLine("\t\tScalefactor " + scale);

				sw.WriteLine("\t}");
			}
		}

		// main function
        [STAThread]
		static void Main()
		{
			object tmpObj = new object();

			// get the source Excel file
			String sourceFileName = "";
			tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2items", "SourceFile", "");
			if (tmpObj != null)
				sourceFileName = tmpObj.ToString();

			// get the root def directory for the items to be exported (i.e. the directory to which the scope will be appended)
			String itemDestDir = "";
			tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2items", "ItemDestDir", "");
			if (tmpObj != null)
				itemDestDir = tmpObj.ToString();

			// prompt user to confirm previous file selections
			bool promptForFiles = true;
			if (sourceFileName != "" && itemDestDir != "")
			{
				String msg = "";
				msg = "Excel File:          " + sourceFileName + "\n" +
					  "Item Dest Dir:      " + itemDestDir + "\n" +
					  "Are these selections OK ?";

				if (MessageBox.Show(msg, "Selected Files/Dirs", MessageBoxButtons.YesNo) == DialogResult.Yes)
					promptForFiles = false;
			}

			// if user declines using previous selections (or if this is first time user is using the exporter),
			// prompt for file selections
			if (promptForFiles)
			{
				// prompt for source Excel file
				OpenFileDialog dlgSrc = new OpenFileDialog();
				dlgSrc.Title = "Choose source Excel file";
				dlgSrc.CheckFileExists = true;
				dlgSrc.DefaultExt = "xls";
                dlgSrc.Filter = "Excel files (*.xls)|*.xls|Excel 2007 files (*.xlsx)|*.xlsx|Excel 2007 Macro-enabled files (*.xlsm)|*.xlsm";
				dlgSrc.InitialDirectory = sourceFileName;
				if (dlgSrc.ShowDialog() != DialogResult.OK)
					return;

				sourceFileName = dlgSrc.FileName;
				Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2items", "SourceFile", sourceFileName);

				// prompt for destination item def directory
				FolderBrowserDialog dlgDest = new FolderBrowserDialog();
				dlgDest.Description = "Select destination directory for the exported item defs, to which the scope will be appended";
				dlgDest.SelectedPath = itemDestDir;
				if (dlgDest.ShowDialog() != DialogResult.OK)
					return;

				itemDestDir = dlgDest.SelectedPath.ToString();
				Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\xls2items", "ItemDestDir", itemDestDir);
			}


            DataSet result = null;
            ExcelDataReader.ExcelDataReader spreadsheet = null;
            IExcelDataReader excelReader = null;
            bool excel2007mode = false;
            if (sourceFileName.EndsWith(".xls"))
            {
                FileStream fs = new FileStream(sourceFileName, FileMode.Open, FileAccess.Read, FileShare.Read);
                spreadsheet = new ExcelDataReader.ExcelDataReader(fs);
                fs.Close();
            }
            else
            {
                excel2007mode = true;
                FileStream stream = File.Open(sourceFileName, FileMode.Open, FileAccess.Read);
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

			if (theTables != null)
			{
                foreach (DataTable table in theTables)
				{
					// disregard private tables
					if (table.TableName.ToString().StartsWith("_"))
						continue;

					int labelRow = -1;
					int[] labelCols = new int[labels.Length];
					for (int i = 0; i < labels.Length; i++)
						labelCols[i] = -1;

					// scan the table for labels and remember the columns corresponding to each label;
					for (int row = 0; row < table.Rows.Count; row++)
					{
						int foundLabelCount = 0;

						for (int col = 0; col < table.Columns.Count && foundLabelCount != labels.Length; col++)
						{
							// look for all of the labels in the same row
							for (int labelIdx = 0; labelIdx < labels.Length; labelIdx++)
							{
								if (table.Rows[row][col].ToString().ToLower().Trim() == labels[labelIdx])
								{
									labelCols[labelIdx] = col;
									foundLabelCount++;
								}
							}
						}

						// if all the labels were found in the same row, continue with outputting item defs
						if (foundLabelCount == labels.Length)
						{
							labelRow = row;
							break;
						}
						// if a row is missing columns, just error and return
						else if (foundLabelCount > 0)
							break;
					}

					// if not all column labels were found, error and return
					if (labelRow == -1)
					{
						for (int i = 0; i < labelCols.Length; i++)
						{
							if (labelCols[i] == -1)
								addMessage(true, "ERROR: Missing column \"" + labels[i] + "\" from table \"" + table.TableName.ToString() + "\".");
						}
						addMessage(true, "ERROR: Table \"" + table.TableName.ToString() + "\" has errors; skipping.");
						continue;
					}

                    //round off floats to 8 decimal places to prevent outputting in e^x notation
                    for (int row = 0; row < table.Rows.Count; row++)
                    {
                        for (int col = 0; col < table.Columns.Count; col++)
                        {
                            double parsedval = 0;
                            if (table.Rows[row][col].GetType().Name == "String")
                                if (double.TryParse(table.Rows[row][col].ToString(), out parsedval))
                                {
                                    table.Rows[row][col] = Math.Round(parsedval, 8);
                                }
                        }
                    }

					// iterate through table rows (starting with the row after the label row) and output item defs
					for (int row = labelRow + 1; row < table.Rows.Count; row++)
					{
						DataRow dataRow = table.Rows[row];
						String itemName = dataRow[labelCols[(int)Columns.NAME]].ToString().Trim();
						String itemScope = dataRow[labelCols[(int)Columns.SCOPE]].ToString().Trim();

						// ensure required fields (name and scope) are populated
						if (itemName.Length == 0)
						{
							addMessage(true, "ERROR: Item in row " + row + " does not have a name specified; skipping.");
							continue;
						}
						if (itemScope.Length == 0)
						{
							addMessage(true, "ERROR: Item \"" + itemName + "\" does not have a scope specified; skipping.");
							continue;
						}

						// make sure the file is writable
						String itemDirName = itemDestDir + "\\" + itemScope.Replace('/', '\\');
						String itemFileName = itemDirName + "\\" + itemName + ".Item";
						if (File.Exists(itemFileName) && (File.GetAttributes(itemFileName) & FileAttributes.ReadOnly) != 0)
						{
							addMessage(true, "ERROR: File \"" + itemFileName + "\" is read-only (not checked out); skipping export of item \"" + itemName + "\".");
							continue;
						}

						// ensure directory exists before writing the stream
						Directory.CreateDirectory(itemDirName);

						// write the file contents
						using (StreamWriter sw = new StreamWriter(itemFileName))
						{
							// collect the primary item data
							String bag = dataRow[labelCols[(int)Columns.BAG]].ToString().Trim();
							String slot = dataRow[labelCols[(int)Columns.SLOT]].ToString().Trim();
							float durability = stringToFloat(dataRow[labelCols[(int)Columns.DURABILITY]].ToString());
							uint level = stringToUInt(dataRow[labelCols[(int)Columns.LEVEL]].ToString());
							String quality = dataRow[labelCols[(int)Columns.QUALITY]].ToString().Trim();
							String reqSkillType = dataRow[labelCols[(int)Columns.REQ_SKILL_TYPE]].ToString().Trim();
							uint reqSkillLevel = stringToUInt(dataRow[labelCols[(int)Columns.REQ_SKILL_LEVEL]].ToString());
							uint minLevel = stringToUInt(dataRow[labelCols[(int)Columns.MIN_LEVEL]].ToString());
							uint maxLevel = stringToUInt(dataRow[labelCols[(int)Columns.MAX_LEVEL]].ToString());

							// output the file
							sw.WriteLine("ItemDef " + itemName);
							sw.WriteLine("{");
							sw.WriteLine("\tScope " + itemScope);

							// write primary data
							if (bag.Length > 0)
								sw.WriteLine("\tRestrictbagid " + bag);
							if (slot.Length > 0)
								sw.WriteLine("\tRestrictslottype " + slot);
							if (durability > 0)
								sw.WriteLine("\tDurability " + durability);
							if (level > 0)
								sw.WriteLine("\tLevel " + level);
							if (quality.Length > 0)
								sw.WriteLine("\tQuality " + quality);

							// write out statpower entries
							streamWriteStatPower(sw, "Strstat", dataRow[labelCols[(int) Columns.STR]].ToString());
							streamWriteStatPower(sw, "Dexstat", dataRow[labelCols[(int) Columns.DEX]].ToString());
							streamWriteStatPower(sw, "Constat", dataRow[labelCols[(int) Columns.CON]].ToString());
							streamWriteStatPower(sw, "Intstat", dataRow[labelCols[(int) Columns.INT]].ToString());
							streamWriteStatPower(sw, "Egostat", dataRow[labelCols[(int) Columns.EGO]].ToString());
							streamWriteStatPower(sw, "Prestat", dataRow[labelCols[(int) Columns.PRE]].ToString());
							streamWriteStatPower(sw, "Recstat", dataRow[labelCols[(int) Columns.REC]].ToString());
							streamWriteStatPower(sw, "Endstat", dataRow[labelCols[(int) Columns.END]].ToString());
							streamWriteStatPower(sw, "Strstat", dataRow[labelCols[(int) Columns.STR2]].ToString());
							streamWriteStatPower(sw, "Dexstat", dataRow[labelCols[(int) Columns.DEX2]].ToString());
							streamWriteStatPower(sw, "Constat", dataRow[labelCols[(int) Columns.CON2]].ToString());
							streamWriteStatPower(sw, "Intstat", dataRow[labelCols[(int) Columns.INT2]].ToString());
							streamWriteStatPower(sw, "Egostat", dataRow[labelCols[(int) Columns.EGO2]].ToString());
							streamWriteStatPower(sw, "Prestat", dataRow[labelCols[(int) Columns.PRE2]].ToString());
							streamWriteStatPower(sw, "Recstat", dataRow[labelCols[(int) Columns.REC2]].ToString());
							streamWriteStatPower(sw, "Endstat", dataRow[labelCols[(int) Columns.END2]].ToString());

							// finish up outputting primary data
							if (minLevel > 0 || maxLevel > 0 || reqSkillLevel > 0 || reqSkillType.Length > 0)
							{
								sw.WriteLine();
								sw.WriteLine("\tRestriction");
								sw.WriteLine("\t{");

								if (minLevel > 0)
									sw.WriteLine("\t\tMinlevel " + minLevel);
								if (maxLevel > 0)
									sw.WriteLine("\t\tMaxlevel " + maxLevel);
								if (reqSkillType.Length > 0)
									sw.WriteLine("\t\tSkilltype " + reqSkillType);
								if (reqSkillLevel > 0)
									sw.WriteLine("\t\tSkilllevel " + reqSkillLevel);

								sw.WriteLine("\t}");
							}
							//sw.WriteLine("\tCraftRecipe Blueprint_" + itemName);
							sw.WriteLine("}");

							// finish writing
							sw.Close();

							// echo success
							addMessage(false, "SUCCESS: Exported item \"" + itemName + "\" to file \"" + itemFileName + "\".");
						}
					}
				}
			}

            if (excel2007mode)
                excelReader.Close();
			if (errorFlag)
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
	}
}