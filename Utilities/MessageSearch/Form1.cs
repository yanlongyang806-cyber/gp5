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

namespace MessageSearch
{
    public partial class Form1 : Form
    {
        static bool ControlDown;

        public Form1()
        {
            InitializeComponent();
        }

        private void listBox1_SelectedIndexChanged(object sender, EventArgs e)
        {
        }

        private void textBox1_TextChanged(object sender, EventArgs e)
        {
        }
    
        private void SearchClicked(object sender, EventArgs e)
        {
            Program.SearchMessages();
        }



        private void listBox1_KeyPress(object sender, KeyPressEventArgs e)
        {
            if (ControlDown && (e.KeyChar == 3 || e.KeyChar == (Char)Keys.C ) ) 
            {
                Clipboard.SetData(DataFormats.Text, ((ListBox)sender).Text);
                e.Handled = true;
            }
        }

        private void listBox1_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Control)
                ControlDown = true;
        }

        private void listBox1_KeyUp(object sender, KeyEventArgs e)
        {
            if (!e.Control)
                ControlDown = false;
        }


    }
}