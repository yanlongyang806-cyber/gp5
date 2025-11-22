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

namespace MessagePacker
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();
        }

        private void ClickMsgFolder(object sender, EventArgs e)
        {
            FolderBrowserDialog dlgdest = new FolderBrowserDialog();
            System.Windows.Forms.TextBox TmpBox = (System.Windows.Forms.TextBox)sender;
            String Name = TmpBox.Name.ToString();

            switch (Name)
            {
                case "PrevFolder":
                    dlgdest.SelectedPath = Program.PrevMsgFolder;
                    dlgdest.Description = "Select folder of previous message dump";
                    break;

                case "NewFolder":
                    dlgdest.SelectedPath = Program.NewMsgFolder;
                    dlgdest.Description = "Select folder of previous message dump";
                    break;

                case "CoreFolder":
                    dlgdest.SelectedPath = Program.NewMsgFolder;
                    dlgdest.Description = "Select Core folder";
                    break;

                case "ProjectFolder":
                    dlgdest.SelectedPath = Program.NewMsgFolder;
                    dlgdest.Description = "Select Project folder";
                    break;
            }


            if (dlgdest.ShowDialog() == DialogResult.OK)
            {
                switch (Name)
                {
                    case "PrevFolder":
                        Program.PrevMsgFolder = dlgdest.SelectedPath.ToString();
                        Program.messages.PrevFolder.Text = Program.PrevMsgFolder;
                        Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "PrevMsgFolder", Program.PrevMsgFolder);
                        break;

                    case "NewFolder":
                        Program.NewMsgFolder = dlgdest.SelectedPath.ToString();
                        Program.messages.NewFolder.Text = Program.NewMsgFolder;
                        Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "NewMsgFolder", Program.NewMsgFolder);
                        break;

                    case "CoreFolder":
                        Program.CoreFolder = dlgdest.SelectedPath.ToString();
                        Program.messages.CoreFolder.Text = Program.CoreFolder;
                        Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "CoreFolder", Program.CoreFolder);
                        break;

                    case "ProjectFolder":
                        Program.ProjectFolder = dlgdest.SelectedPath.ToString();
                        Program.messages.ProjectFolder.Text = Program.ProjectFolder;
                        Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "ProjectFolder", Program.ProjectFolder);
                        break;
                }

            }
        }

        private void AddFolderEntry(object sender, EventArgs e)
        {
            FolderBrowserDialog dlgdest = new FolderBrowserDialog();
            System.Windows.Forms.ListBox TmpBox = (System.Windows.Forms.ListBox)sender;
            String Name = TmpBox.Name.ToString();

            switch (Name)
            {
                case "listBox2":
                    dlgdest.SelectedPath = "";
                    dlgdest.Description = "Select a Client message folder";
                    break;
            }

            if (dlgdest.ShowDialog() == DialogResult.OK)
            {
                String Folders = "";
                Program.ClientFolders[Program.ClientFolderCount] = dlgdest.SelectedPath.ToString();

                switch (Name)
                {
                    case "listBox2":
                        Program.messages.listBox2.Items.Add(dlgdest.SelectedPath.ToString());

                        Program.ClientFolderCount = Program.messages.listBox2.Items.Count;
                        Folders = "";
                        for (int ii = 0; ii < Program.messages.listBox2.Items.Count; ii++)
                        {
                            Program.ClientFolders[ii] = Program.messages.listBox2.Items[ii].ToString();

                            if (Folders != "")
                                Folders = Folders + ";";

                            Folders = Folders + Program.ClientFolders[ii];
                        }
                        Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "ClientFolders", Folders);
                        break;
                }
            }
        }

        private void ModifyFolderEntry(object sender, EventArgs e)
        {
            FolderBrowserDialog dlgdest = new FolderBrowserDialog();
            System.Windows.Forms.ToolStripMenuItem TmpMenuItem = (System.Windows.Forms.ToolStripMenuItem)sender;
            System.Windows.Forms.ToolStrip TmpMenu = (System.Windows.Forms.ToolStrip)TmpMenuItem.GetCurrentParent();
            String Name = TmpMenu.Name.ToString();

 
            switch (Name)
            {
                case "contextMenuStrip1":
                case "listbox2Menu":
                    dlgdest.SelectedPath = "";
                    dlgdest.Description = "Select a Client message folder";
                    break;
            }
            
            if (dlgdest.ShowDialog() == DialogResult.OK)
            {
                int idx = 0;

                    switch (Name)
                    {
                        case "contextMenuStrip1":
                        case "listbox2Menu":
                            idx = Program.messages.listBox2.SelectedIndex;

                            if (idx >= 0)
                            {
                                Program.messages.listBox2.Items[idx] = dlgdest.SelectedPath.ToString();

                                String Folders = "";
                                for (int ii = 0; ii < Program.messages.listBox2.Items.Count; ii++)
                                {
                                    Program.ClientFolders[ii] = Program.messages.listBox2.Items[ii].ToString();

                                    if (Folders != "")
                                        Folders = Folders + ";";

                                    Folders = Folders + Program.ClientFolders[ii];
                                }
                                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "ClientFolders", Folders);
                            }
                            break;
                    }
            }
        }

        private void DeleteFolderEntry(object sender, EventArgs e)
        {
            System.Windows.Forms.ToolStripMenuItem TmpMenuItem = (System.Windows.Forms.ToolStripMenuItem)sender;
            System.Windows.Forms.ToolStrip TmpMenu = (System.Windows.Forms.ToolStrip)TmpMenuItem.GetCurrentParent();
            String Name = TmpMenu.Name.ToString();
            int idx = 0;

            switch (Name)
            {
                case "contextMenuStrip1":
                case "listbox2Menu":
                    idx = Program.messages.listBox2.SelectedIndex;

                    if (idx >= 0)
                    {
                        Program.messages.listBox2.Items.RemoveAt(idx);

                        Program.ClientFolderCount = Program.messages.listBox2.Items.Count;
                        String Folders = "";
                        for (int ii = 0; ii < Program.messages.listBox2.Items.Count; ii++)
                        {
                            Program.ClientFolders[ii] = Program.messages.listBox2.Items[ii].ToString();

                            if (Folders != "")
                                Folders = Folders + ";";

                            Folders = Folders + Program.ClientFolders[ii];
                        }
                        Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "ClientFolders", Folders);
                    }
                    break;
            }
          
        }

        private void Dups_checkBox_CheckedChanged(object sender, EventArgs e)
        {
            switch (Program.messages.Dups_checkBox.Checked)
            {
                case false:
                    Program.bRemoveDuplicates = false;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "RemoveDuplicates", "false");
                    break;

                case true:
                    Program.bRemoveDuplicates = true;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "RemoveDuplicates", "true");
                    break;
            }
        }

        private void dummytrans_checkbox_CheckedChanged(object sender, EventArgs e)
        {
            switch (Program.messages.dummytrans_checkbox.Checked)
            {
                case false:
                    Program.bCreateDummyFiles = false;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "CreateDummyFiles", "false");
                    break;

                case true:
                    Program.bCreateDummyFiles = true;
                    Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "CreateDummyFiles", "true");
                    break;
            }
        }

        private void listBox2_SelectedIndexChanged(object sender, EventArgs e)
        {

        }
    }
}