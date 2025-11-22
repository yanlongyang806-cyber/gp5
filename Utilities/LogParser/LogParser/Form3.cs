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

namespace LogParser
{
    public partial class LogParserSetup : Form
    {
        public LogParserSetup()
        {
            InitializeComponent();
        }

        private void ClickChooseLog(object sender, EventArgs e)
        {
            OpenFileDialog dlgsrc = new OpenFileDialog();
            DialogResult res = DialogResult.None;

            dlgsrc.Title = "Choose source Log file";
            dlgsrc.CheckFileExists = true;
            dlgsrc.DefaultExt = "*";
            dlgsrc.Filter = "Log files (*)|*";
            dlgsrc.InitialDirectory = textBox1.Text;

            while ((res = dlgsrc.ShowDialog()) == DialogResult.None) ;

            if (res == DialogResult.OK)
            {
                textBox1.Text = dlgsrc.FileName;
            }
        }
     }
}