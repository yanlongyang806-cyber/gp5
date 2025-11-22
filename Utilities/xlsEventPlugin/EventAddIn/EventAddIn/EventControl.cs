using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Data;
using System.Linq;
using System.Text;
using System.Windows.Forms;
using System.Windows.Forms.Design;
using System.Globalization;

namespace EventAddIn
{
    public partial class EventControl : UserControl
    {
        Func<Boolean> exportFunc;
        ExportSettings exportSettings;

        public EventControl()
        {
            InitializeComponent();
            exportSettings = new ExportSettings();
            propertyGrid1.SelectedObject = exportSettings;
        }

        public void EventControl_SetExportCallback(Func<Boolean> newExportFunc)
        {
            exportFunc = newExportFunc;
        }

        private void btnExport_Click(object sender, EventArgs e)
        {
            exportFunc();
        }

        public String GetStartTime()
        {
            return exportSettings.DateBegin;
        }

        public void SetStartTime(String strStartTime)
        {
            exportSettings.DateBegin = strStartTime;
        }

        public String GetRowTime()
        {
            return exportSettings.RowBlockTime;
        }

        public void SetRowTime(String strRowTime)
        {
            exportSettings.RowBlockTime = strRowTime;
        }

        public String GetEndTime()
        {
            return exportSettings.DateEnd;
        }

        public String getSaveSettings()
        {
            String sReturn = null;

            sReturn = String.Format("Save Data#{0}#{1}#{2}#{3}", exportSettings.FileName, exportSettings.RowBlockTime, exportSettings.DateBegin, exportSettings.DateEnd);

            return sReturn;
        }

        public void LoadSettings(String sSettings)
        {
            char[] delims = {'#'};
            String[] words = sSettings.Split(delims);

            exportSettings.FileName = words[1];
            exportSettings.RowBlockTime = words[2];
            exportSettings.DateBegin = words[3];
            exportSettings.DateEnd = words[4];
        }

        public String getFileName()
        {
            return exportSettings.FileName;
        }
    }

    public class ExportSettings
    {
        private string strFileName = "C:/ExportData.Events";
        private string strStartDate = DateTime.Now.ToString("MM/dd/yyyy HH:mm");
        private string strEndDate = DateTime.Now.ToString("MM/dd/yyyy HH:mm");
        private string strRowTime = "01:00";
        //private string _customFilter = "Event Files(*.events)|*.events";

        /*
        [DefaultValue("Event Files(*.events)|*.events")]
        public string CustomFilter
        {
            get { return _customFilter; }
            set {_customFilter = value; }
        }
        */

        [CategoryAttribute("Export"),
        DescriptionAttribute("The File Where this will export to.")]
        public string FileName
        {
            get { return strFileName;}
            set { strFileName = value;}
        }

        [CategoryAttribute("Timing"),
        DescriptionAttribute("Each row in the calendar represents this much time"),
        Editor(typeof(TimePickerEditor), typeof(System.Drawing.Design.UITypeEditor))]
        public string RowBlockTime
        {
            get { return strRowTime; }
            set { strRowTime = value; }
        }

        [CategoryAttribute("Timing"),
        DescriptionAttribute("The Date when these events begin"),
        Editor(typeof(DateTimePickerEditor), typeof(System.Drawing.Design.UITypeEditor))]
        public string DateBegin
        {
            get { return strStartDate; }
            set { strStartDate = value; }
        }

        [CategoryAttribute("Timing"),
        DescriptionAttribute("The Date when these events end"),
        Editor(typeof(DateTimePickerEditor), typeof(System.Drawing.Design.UITypeEditor))]
        public string DateEnd
        {
            get { return strEndDate; }
            set { strEndDate = value; }
        }


        internal class TimePickerEditor : System.Drawing.Design.UITypeEditor
        {
            IWindowsFormsEditorService editorService;
            DateTimePicker picker = new DateTimePicker();
            string time;

            public TimePickerEditor()
            {
                picker.Format = DateTimePickerFormat.Custom;
                picker.CustomFormat = "HH:mm";
                picker.ShowUpDown = true;
                
            }

            public override object EditValue(System.ComponentModel.ITypeDescriptorContext context, IServiceProvider provider, object value)
            {
                if (provider != null)
                    this.editorService = provider.GetService(typeof(IWindowsFormsEditorService)) as IWindowsFormsEditorService;

                if (this.editorService != null)
                {
                    if (value == null)
                    {
                        time = "01:00"; //default value, 1 hour
                    }

                    this.editorService.DropDownControl(picker);

                    value = picker.Value.ToString("HH:mm");
                }

                return value;
            }

            public override System.Drawing.Design.UITypeEditorEditStyle GetEditStyle(System.ComponentModel.ITypeDescriptorContext context)
            {
                return System.Drawing.Design.UITypeEditorEditStyle.DropDown;
            }
        }

        internal class DateTimePickerEditor : System.Drawing.Design.UITypeEditor
        {

            IWindowsFormsEditorService editorService;
            DateTimePicker picker = new DateTimePicker();
            string time;

            public DateTimePickerEditor()
            {
                
                picker.Format = DateTimePickerFormat.Custom;
                picker.CustomFormat = "MM/dd/yyyy HH:mm";
                
                picker.ShowUpDown = true;

            }

            public override object EditValue(System.ComponentModel.ITypeDescriptorContext context, IServiceProvider provider, object value)
            {
                CultureInfo CultureInfoProvider = CultureInfo.InvariantCulture;

                if (provider != null)
                {
                    this.editorService = provider.GetService(typeof(IWindowsFormsEditorService)) as IWindowsFormsEditorService;
                }

                if (this.editorService != null)
                {
                    if (value == null)
                    {
                        time = DateTime.Now.ToString("MM/dd/yyyy HH:mm");
                    }

                    this.editorService.DropDownControl(picker);
                    
                    value = picker.Value.ToString("MM/dd/yyyy HH:mm");
                }

                return value;

            }

            public void SetValue(string sValue)
            {
                CultureInfo CultureInfoProvider = CultureInfo.InvariantCulture;
                time = sValue;
                picker.Value = DateTime.ParseExact(sValue,"HH:mm",CultureInfoProvider);
            }

            public override System.Drawing.Design.UITypeEditorEditStyle GetEditStyle(System.ComponentModel.ITypeDescriptorContext context)
            {
                return System.Drawing.Design.UITypeEditorEditStyle.DropDown;
            }

        }

    }
}
