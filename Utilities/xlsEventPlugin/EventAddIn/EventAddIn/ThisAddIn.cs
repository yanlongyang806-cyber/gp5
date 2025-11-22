using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Xml.Linq;
using Excel = Microsoft.Office.Interop.Excel;
using Office = Microsoft.Office.Core;
using Microsoft.Office.Tools.Excel;
using System.Globalization;

// We assume that there will be enough rows for the given RowBlockTime in the controller.
// We used to count the number of rows in the first day's column that were non-empty, but that
//  does not work with Merge Areas and does not allow us to have empty slots.

namespace EventAddIn
{
    public partial class ThisAddIn
    {
        private EventControl myEventControl;
        private Microsoft.Office.Tools.CustomTaskPane myCustomTaskPane;

        private String[] ColumnToString = { "0", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z" };

        private String ThisAddIn_GetCurrentDateString(String sPrintString)
        {
            return String.Format("{0}: {1}", sPrintString, DateTime.Now.ToString("MM/dd/yyyy H:mm:ss"));
        }

        private String GetStringForColumnInt(int iColumn)
        {
            if (iColumn <= 26)
                return ColumnToString[iColumn];
            else
            {
                return String.Format("{0}{1}",GetStringForColumnInt(iColumn/26),GetStringForColumnInt(iColumn%26));
            }
        }

        private void ThisAddIn_PrintDateString(String sNameString)
        {
            Excel.Worksheet activeWorksheet = ((Excel.Worksheet)Application.ActiveSheet);
            Excel.Range last = activeWorksheet.Cells.SpecialCells(Excel.XlCellType.xlCellTypeLastCell, Type.Missing);
            Excel.Range searchRange = activeWorksheet.get_Range("A1", last);

            Excel.Range formatRange = searchRange.Find(sNameString, missing,
                            Excel.XlFindLookIn.xlValues, Excel.XlLookAt.xlPart, Excel.XlSearchOrder.xlByColumns,
                            Excel.XlSearchDirection.xlNext, false, missing, missing);

            if (formatRange == null)
            {
                String sRange = String.Format("A{0}", searchRange.Rows.Count + 2);
                formatRange = activeWorksheet.get_Range(sRange);
            }

            if (formatRange != null)
            {
                formatRange.Value2 = ThisAddIn_GetCurrentDateString(sNameString);
            }
        }

        private void ThisAddIn_PrintLastSaveDate()
        {
            ThisAddIn_PrintDateString("Last Save");
        }

        private void ThisAddIn_PrintLastExportDate()
        {
            ThisAddIn_PrintDateString("Last Export");
        }

        private void ThisAddIn_LoadData()
        {
            Excel.Worksheet activeWorksheet = ((Excel.Worksheet)Application.ActiveSheet);
            Excel.Range findData = activeWorksheet.Cells.Find("Save Data#",missing,Excel.XlFindLookIn.xlValues, Excel.XlLookAt.xlPart, Excel.XlSearchOrder.xlByColumns,
                                                                Excel.XlSearchDirection.xlNext, false, missing, missing);

            if(findData != null)
            {
                myEventControl.LoadSettings(findData.Value2);
            }
        }

        private void ThisAddIn_SaveData()
        {
            Excel.Worksheet activeWorksheet = ((Excel.Worksheet)Application.ActiveSheet);
            Excel.Range findData = activeWorksheet.Cells.Find("Save Data#",missing,Excel.XlFindLookIn.xlValues, Excel.XlLookAt.xlPart, Excel.XlSearchOrder.xlByColumns,
                                                                Excel.XlSearchDirection.xlNext, false, missing, missing);

            if(findData == null)
            {
                Excel.Range last = activeWorksheet.Cells.SpecialCells(Excel.XlCellType.xlCellTypeLastCell, Type.Missing);
                findData = activeWorksheet.get_Range("A1", last);

                findData = activeWorksheet.get_Range(String.Format("A{0}",findData.Rows.Count + 1));
            }

            findData.Value2 = myEventControl.getSaveSettings();
        }

        private Excel.Range ThisAddIn_FindEventDefRange()
        {
            Excel.Worksheet activeWorksheet = ((Excel.Worksheet)Application.ActiveSheet);
            Excel.Range last = activeWorksheet.Cells.SpecialCells(Excel.XlCellType.xlCellTypeLastCell, Type.Missing);
            Excel.Range searchRange = activeWorksheet.get_Range("A1", last);

            Excel.Range EventDefsRange = searchRange.Find("EventDef");

            return EventDefsRange;
        }

        private Excel.Range ThisAddIn_FindCalendarRange()
        {
            Excel.Worksheet activeWorksheet = ((Excel.Worksheet)Application.ActiveSheet);
            Excel.Range last = activeWorksheet.Cells.SpecialCells(Excel.XlCellType.xlCellTypeLastCell, Type.Missing);
            Excel.Range searchRange = activeWorksheet.get_Range("A1", last);

            Excel.Range CalendarRange = searchRange.Find("Event Schedule");

            return CalendarRange;
        }

        private int ThisAddIn_GetCalendarDays(Excel.Range CalendarRange)
        {
			// Count the day headers across the sheet to determine how many days the cycle runs

            Excel.Worksheet activeWorksheet = ((Excel.Worksheet)Application.ActiveSheet);

            int iDays = 0;

            int iRow = CalendarRange.Row + 1;
            int iCol = CalendarRange.Column;

            Excel.Range checkRange = activeWorksheet.get_Range(String.Format("{0}{1}", GetStringForColumnInt(iCol), iRow));

            while (checkRange != null && checkRange.Value2 != null && String.Compare(checkRange.Value2, "") != 0)
            {
                iCol++;
                iDays++;
                checkRange = activeWorksheet.get_Range(String.Format("{0}{1}", GetStringForColumnInt(iCol), iRow));
            }

            return iDays;
        }

        private int ThisAddIn_GetCalendarBlocks(Excel.Range CalendarRange)
        {
            Excel.Worksheet activeWorksheet = ((Excel.Worksheet)Application.ActiveSheet);
            int iBlocks = 0;

            int iRow = CalendarRange.Row + 2;
            int iCol = CalendarRange.Column;

            Excel.Range checkRange = activeWorksheet.get_Range(String.Format("{0}{1}", GetStringForColumnInt(iCol), iRow));

            while (checkRange != null && checkRange.Value2 != null && String.Compare(checkRange.Value2, "") != 0)
            {
                iRow++;
                iBlocks++;
                checkRange = activeWorksheet.get_Range(String.Format("{0}{1}", GetStringForColumnInt(iCol), iRow));
            }

            return iBlocks;
        }

		private void WriteOneEventBlock(System.IO.StreamWriter file, DateTime OrigStart, DateTime OrigEnd,
												DateTime PACPeriodStart, DateTime PACPeriodEnd, int iIsDST,
												int iCycleSeconds, int iDuration)
		{
			// Adjust for PAC on the period times
			DateTime PeriodStart;
			DateTime PeriodEnd;

			if (iIsDST==1)
			{
				PeriodStart = PACPeriodStart.AddHours(8);
				PeriodEnd = PACPeriodEnd.AddHours(7);
			}
			else
			{
				PeriodStart = PACPeriodStart.AddHours(7);
				PeriodEnd = PACPeriodEnd.AddHours(8);
			}
			int iPeriodOffsetSeconds = 0;
			if (iIsDST==1)
			{
				// In DST, things have to play one hour earlier to match up to the clock time.
				iPeriodOffsetSeconds = -60*60;
			}

			// Pretend the orignal dates are in the offset period so the cycle calculation does the correct thing
			DateTime offsetOrigStart = OrigStart.AddSeconds(iPeriodOffsetSeconds);
			DateTime offsetOrigEnd = OrigEnd.AddSeconds(iPeriodOffsetSeconds);

			if (offsetOrigStart > PeriodEnd || offsetOrigEnd < PeriodStart)
			{
				return;
			}

			DateTime dateStart;
			DateTime dateEnd;

			if (offsetOrigStart >= PeriodStart)
			{
				dateStart = offsetOrigStart;
			}
			else
			{
				TimeSpan delta = PeriodStart - offsetOrigStart;
				int iDiffSeconds = (int)delta.TotalSeconds;
				int iSecondsFromCycleStart = iDiffSeconds % iCycleSeconds;

				if (iSecondsFromCycleStart==0)
				{
					dateStart = PeriodStart;
				}
				else
				{
					dateStart = PeriodStart.AddSeconds(iCycleSeconds - iSecondsFromCycleStart);
				}
			}

			if (offsetOrigEnd >= PeriodEnd)
			{
				dateEnd = PeriodEnd;
			}
			else
			{
				dateEnd = offsetOrigEnd;
			}

			file.WriteLine("          EventTime");
			file.WriteLine("          {");
			//String dateStartString = dateStart.ToString("yyyy-mm-dd HH:mm:00");

			file.WriteLine(String.Format("                StartDate \"UTC{0}\"", dateStart.ToString("yyyy-MM-dd HH:mm:00")));
			file.WriteLine(String.Format("                EndDate \"UTC{0}\"", dateEnd.ToString("yyyy-MM-dd HH:mm:00")));
			file.WriteLine(String.Format("                TimeRepeat {0}", iCycleSeconds));
			file.WriteLine(String.Format("                TimeDuration {0}", iDuration));
			file.WriteLine("          }");
		}

        public bool ExportData()
        {
            Excel.Range EventDefRange = ThisAddIn_FindEventDefRange();
            Excel.Range CalendarRange = ThisAddIn_FindCalendarRange();
            Excel.Worksheet activeWorksheet = ((Excel.Worksheet)Application.ActiveSheet);

            if (EventDefRange == null)
                return false;
            //Create a file

            using (System.IO.StreamWriter file = new System.IO.StreamWriter(myEventControl.getFileName(), false))
            {
                file.WriteLine(String.Format("# This File Was exported on {0}",DateTime.Now.ToString("MM/dd/yyyy H:mm:ss")));

                file.WriteLine("{");

                int iRow = EventDefRange.Row + 1;
                int iColumn = EventDefRange.Column;

                int iCalendarRow = CalendarRange.Row + 2; // Skip a row for headers
                int iCalendarColumn = CalendarRange.Column;
                int iCalendarDays = ThisAddIn_GetCalendarDays(CalendarRange);
                CultureInfo provider = CultureInfo.InvariantCulture;
                DateTime dateBlock = DateTime.ParseExact(myEventControl.GetRowTime(), "HH:mm", provider);
                DateTime calendarStart = DateTime.ParseExact(myEventControl.GetStartTime(), "MM/dd/yyyy HH:mm", provider);
                DateTime calendarEnd = DateTime.ParseExact(myEventControl.GetEndTime(), "MM/dd/yyyy HH:mm", provider);
                int iRepeatSeconds = iCalendarDays * 24 * 60 * 60;
                int iBlockSeconds = (dateBlock.Hour * 60 * 60) + dateBlock.Minute * 60;
                int iCalendarBlocks = 1440/(dateBlock.Hour*60+dateBlock.Minute);	// Does not allow scheduling events in any left-over minutes

                bool bContinueSearch = true;

                while(bContinueSearch)
                {

                    Excel.Range NewEvent = activeWorksheet.get_Range(String.Format("{0}{1}", GetStringForColumnInt(iColumn), iRow));
                    int ic = iColumn;

                    if (NewEvent != null && NewEvent.Value2 != null && String.Compare(NewEvent.Value2,"") != 0)
                    {
                        String strDefAddress = null;
                        String strDefName = null;
                        while (true)
                        {
                            // Get all the properties
                            Excel.Range propertyTitle = activeWorksheet.get_Range(String.Format("{0}{1}", GetStringForColumnInt(ic), EventDefRange.Row));
                            Excel.Range propertyValue = activeWorksheet.get_Range(String.Format("{0}{1}", GetStringForColumnInt(ic), iRow));

                            propertyValue.get_Address();

                            if (propertyTitle != null && propertyTitle.Value2 != null && String.IsNullOrWhiteSpace(propertyTitle.Value2) == false
                                && propertyValue != null && propertyValue.Value2 != null && String.IsNullOrWhiteSpace(propertyValue.Value2) == false)
                            {
                                if (String.Compare(propertyTitle.Value2, "EventDef", true) == 0)
                                {
                                    file.WriteLine(String.Format("     Event {0}", propertyValue.Value2));
                                    file.WriteLine("     {");
                                    strDefAddress = propertyValue.get_Address();
                                    strDefName = propertyValue.Value2;
                                }
                                else if (propertyTitle.Value2.IndexOf("Warp") >= 0)
                                {
                                    if(String.IsNullOrWhiteSpace(propertyValue.Value2) == false)
                                    {
                                        char[] delims = {'#'};
                                        String[] sSubString = propertyValue.Value2.Split(delims);

                                        file.WriteLine(String.Format("          {0}", propertyTitle.Value2));
                                        file.WriteLine("          {");
                                        foreach (String s in sSubString)
                                        {
                                            file.WriteLine(String.Format("                {0}", s));
                                        }
                                        file.WriteLine("          }");

                                    }
                                }
                                else if (String.Compare(propertyTitle.Value2,"Activities") == 0
                                    || String.Compare(propertyTitle.Value2, "Activity") == 0)
                                {
                                    char[] delims = {'#'};
                                    String[] sSubStrings = propertyValue.Value2.Split(delims);

                                    foreach (String s in sSubStrings)
                                    {
                                        if (String.IsNullOrWhiteSpace(s) == false)
                                            file.WriteLine(String.Format("          Activity {0}", s));
                                    }

                                }
                                else
                                {
                                    char[] delims = { '#' };
                                    String[] sSubStrings = propertyValue.Value2.Split(delims);

                                    foreach (String s in sSubStrings)
                                    {
                                        if (String.IsNullOrWhiteSpace(s) == false)
                                            file.WriteLine(String.Format("          {0} {1}", propertyTitle.Value2, s));
                                    }
                                }
                            }
                            else if (propertyTitle == null || propertyTitle.Value2 == null || String.IsNullOrWhiteSpace(propertyTitle.Value2) == true)
                            {
                                break;
                            }

                            ic++;
                        }

                        //Search the calendar for all possible instances of this for the timing info

                        Excel.Range newInstance = activeWorksheet.Cells.Find(strDefName, missing,
                            Excel.XlFindLookIn.xlValues, Excel.XlLookAt.xlWhole, Excel.XlSearchOrder.xlByColumns,
                            Excel.XlSearchDirection.xlNext, false, missing, missing);

                        Excel.Range firstFind = null;

                        while (newInstance != null && (firstFind == null || String.Compare(newInstance.get_Address(), firstFind.get_Address()) != 0))
                        {
                            if (firstFind == null)
                                firstFind = newInstance;

                            if (String.Compare(newInstance.get_Address(), strDefAddress) == 0)
                                goto next;

                            int iDay = newInstance.Column - iCalendarColumn;
                            int iBlockStart = newInstance.Row - iCalendarRow;
                            int iMergedRows = 0;
                            
                            DateTime dateStart = calendarStart;
                            dateStart = dateStart.AddHours(dateBlock.Hour * iBlockStart);
                            dateStart = dateStart.AddMinutes(dateBlock.Minute * iBlockStart);

                            dateStart = dateStart.AddDays(iDay);

                            iMergedRows +=  newInstance.MergeArea.Rows.Count;

                            if (iDay < 0 || iBlockStart < 0)
                                goto next;
 
                            if (iDay >= iCalendarDays || iBlockStart >= iCalendarBlocks)
                                goto next;

// Write out separate timeEvents for each daylight savings/standard time period. This lets us keep events happening at the same PAC clock time.
//2012 	March 11 	November 04
//2013 	March 10 	November 03
//2014 	March 09 	November 02
//2015 	March 08 	November 01
//2016 	March 13 	November 06
//2017 	March 12 	November 05
//2018 	March 11 	November 04
//2019 	March 10 	November 03
//2020 	March 08 	November 01


							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2007,1,1,  2,0,0), new DateTime (2012,3,11, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2012, 3,11, 2,0,0), new DateTime (2012,11, 4, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2012,11, 4, 2,0,0), new DateTime (2013, 3,10, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2013, 3,10, 2,0,0), new DateTime (2013,11, 3, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2013,11, 3, 2,0,0), new DateTime (2014, 3, 9, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2014, 3, 9, 2,0,0), new DateTime (2014,11, 2, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2014,11, 2, 2,0,0), new DateTime (2015, 3, 8, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2015, 3, 8, 2,0,0), new DateTime (2015,11, 1, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2015,11, 1, 2,0,0), new DateTime (2016, 3,13, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2016, 3,13, 2,0,0), new DateTime (2016,11, 6, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2016,11, 6, 2,0,0), new DateTime (2017, 3,12, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2017, 3,12, 2,0,0), new DateTime (2017,11, 5, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2017,11, 5, 2,0,0), new DateTime (2018, 3,11, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2018, 3,11, 2,0,0), new DateTime (2018,11, 4, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2018,11, 4, 2,0,0), new DateTime (2019, 3,10, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2019, 3,10, 2,0,0), new DateTime (2019,11, 3, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2019,11, 3, 2,0,0), new DateTime (2020, 3, 8, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2020, 3, 8, 2,0,0), new DateTime (2020,11, 1, 2,0,0), 1,
								iRepeatSeconds, iBlockSeconds * iMergedRows);
							WriteOneEventBlock(file, dateStart, calendarEnd, new DateTime(2020,11, 1, 2,0,0), new DateTime (2020,12,31, 2,0,0), 0,
								iRepeatSeconds, iBlockSeconds * iMergedRows);

                        next:
                            newInstance = activeWorksheet.Cells.FindNext(newInstance);
                        }

                        iRow++;
                        file.WriteLine("     }");
                       
                    }
                    else
                    {
                        bContinueSearch = false;
                    }
                }

                file.WriteLine("}");
            } 
            //Find all individual events
            


            ThisAddIn_PrintLastExportDate();

            return true;
        }

        private bool ThisAddIn_IsCalendarWorksheet()
        {
            Excel.Range CalendarRange = ThisAddIn_FindCalendarRange();

            if (CalendarRange != null)
                return true;

            return false;
        }

        private void ThisAddIn_Startup(object sender, System.EventArgs e)
        {
            myEventControl = new EventControl();

            this.Application.WorkbookBeforeSave += new Microsoft.Office.Interop.Excel.AppEvents_WorkbookBeforeSaveEventHandler(Application_WorkbookBeforeSave);
            this.Application.WorkbookOpen += new Microsoft.Office.Interop.Excel.AppEvents_WorkbookOpenEventHandler(Application_WorkBookOpen);
            
        }

        private void ThisAddIn_Shutdown(object sender, System.EventArgs e)
        {
            
        }

        void Application_WorkbookBeforeSave(Microsoft.Office.Interop.Excel.Workbook Wb, bool SaveAsUI, ref bool Cancel)
        {
            if (ThisAddIn_IsCalendarWorksheet())
            {
                ThisAddIn_PrintLastSaveDate();
                ThisAddIn_SaveData();
            }
        }

        void Application_WorkBookOpen(Microsoft.Office.Interop.Excel.Workbook Wb)
        {
            ThisAddIn_LoadData();

            if (ThisAddIn_IsCalendarWorksheet())
            {
                myCustomTaskPane = this.CustomTaskPanes.Add(myEventControl, "Event Control");
                myCustomTaskPane.Visible = true;

                myEventControl.EventControl_SetExportCallback(ExportData);
            }
        }
        #region VSTO generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InternalStartup()
        {
            this.Startup += new System.EventHandler(ThisAddIn_Startup);
            this.Shutdown += new System.EventHandler(ThisAddIn_Shutdown);
        }
        
        #endregion
    }
}
