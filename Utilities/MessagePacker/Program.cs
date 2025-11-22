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
using System.Text.RegularExpressions;

namespace MessagePacker
{
    static class Program
    {

        public static Form1 messages = new Form1();
        static bool error_flag = false;

        struct Msg
        {
            public String MessageKey;
            public String Scope;
            public String Description;
            public String DefaultString;
            public bool DuplicateMsgKey;
            public bool DuplicateMsg;
            public String DuplicateOfKey;
            public bool MsgUpdated;
            public bool ClientMsg;
            public bool CoreMsg;
            public String FileName;
        }

        struct ScopeData
        {
            public String Scope;
            public int count;
        }


        const int MAX_MESSAGES = (128 * 1024);

        static Msg[] PreviousMsgs = new Msg[MAX_MESSAGES];
        static int previous_messagecount = 0;

        static Msg[] Msgs = new Msg[MAX_MESSAGES];
        static int messagecount = 0;

        static ScopeData[] Scopes = new ScopeData[MAX_MESSAGES];
        static int scopecount = 0;

        static int totalmessagecount = 0;
        static int clientmessagecount = 0;
        static int servermessagecount = 0;
        static int coreclientmessagecount = 0;
        static int coreservermessagecount = 0;
        static int wordcount = 0;

        static int nodup_totalmessagecount = 0;
        static int nodup_clientmessagecount = 0;
        static int nodup_servermessagecount = 0;
        static int nodup_coreclientmessagecount = 0;
        static int nodup_coreservermessagecount = 0;
        static int nodup_wordcount = 0;

        static void InitMsg(Msg[] Msgs, int idx)
        {
            //Msgs[idx] = new Msg();
            Msgs[idx].MessageKey = "";
            Msgs[idx].Scope = "";
            Msgs[idx].Description = "";
            Msgs[idx].DefaultString = "";
            Msgs[idx].DuplicateMsgKey = false;
            Msgs[idx].DuplicateMsg = false;
            Msgs[idx].DuplicateOfKey = "";
            Msgs[idx].MsgUpdated = false;
            Msgs[idx].ClientMsg = false;
            Msgs[idx].CoreMsg = false;
            Msgs[idx].FileName = "";
        }

        public static String PrevMsgFolder = "";
        public static String NewMsgFolder = "";

        public static String DefaultCoreFolder = "C:\\Core\\data";
        public static String DefaultProjectFolder = "C:\\FightClub\\data";
        public static String DefaultClientFolders = "C:\\Core\\data\\messages;C:\\Core\\data\\ui;C:\\Core\\data\\defs\\costumes\\Definitions;C:\\FightClub\\data\\messages;C:\\FightClub\\data\\ui;C:\\FightClub\\data\\defs\\costumes\\Definitions";
        
        public static String CoreFolder = "";
        public static String ProjectFolder = "";
        public static String[] ClientFolders = new String[128];
        public static int ClientFolderCount = 0;
        public static bool bRemoveDuplicates = false;
        public static bool bCreateDummyFiles = false;

        [STAThread]
        static void Main()
        {
            object tmpObj = new object();
            Char[] folder_delimit = new Char[1];
            folder_delimit[0] = ';';

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "PrevMsgFolder", "");
            if (tmpObj != null)
            {
                PrevMsgFolder = tmpObj.ToString();
            }
            messages.PrevFolder.Text = PrevMsgFolder;

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "NewMsgFolder", "");
            if (tmpObj != null)
            {
                NewMsgFolder = tmpObj.ToString();
            }
            messages.NewFolder.Text = NewMsgFolder;

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "CoreFolder", DefaultCoreFolder);
            if (tmpObj != null)
            {
                CoreFolder = tmpObj.ToString();
            }
            else
            {
                CoreFolder = DefaultCoreFolder;
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "CoreFolder", DefaultCoreFolder);
            }
            messages.CoreFolder.Text = CoreFolder;

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "ProjectFolder", DefaultProjectFolder);
            if (tmpObj != null)
            {
                ProjectFolder = tmpObj.ToString();
            }
            else
            {
                ProjectFolder = DefaultProjectFolder;
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "ProjectFolder", DefaultProjectFolder);
            }
            messages.ProjectFolder.Text = ProjectFolder;

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "ClientFolders", DefaultClientFolders);
            if (tmpObj != null)
            {
                String[] Folders = tmpObj.ToString().Split(folder_delimit);
                ClientFolderCount = Folders.Length;
                for (int ii = 0; ii < ClientFolderCount; ii++)
                {
                    ClientFolders[ii] = Folders[ii];
                }
            }
            else
            {
                String[] Folders = DefaultClientFolders.ToString().Split(folder_delimit);
                Registry.SetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "ClientFolders", DefaultClientFolders);
                ClientFolderCount = Folders.Length;
                for (int ii = 0; ii < ClientFolderCount; ii++)
                {
                    ClientFolders[ii] = Folders[ii];
                }
            }
            messages.listBox2.Items.Clear();
            for (int ii = 0; ii < ClientFolderCount; ii++)
            {
                messages.listBox2.Items.Add(ClientFolders[ii]);
            }

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "RemoveDuplicates", "false");
            if (tmpObj != null)
            {
                if (tmpObj.ToString() == "true" )
                    bRemoveDuplicates = true;
                else
                    bRemoveDuplicates = false;
            }
            messages.Dups_checkBox.Checked = bRemoveDuplicates;

            tmpObj = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Cryptic\\MessagePacker", "CreateDummyFiles", "false");
            if (tmpObj != null)
            {
                if (tmpObj.ToString() == "true")
                    bCreateDummyFiles = true;
                else
                    bCreateDummyFiles = false;
            }
            messages.dummytrans_checkbox.Checked = bCreateDummyFiles;

            for (; ; )
            {
                for (bool done = false; !done; )
                {
                    DialogResult res = messages.ShowDialog();

                    switch (res)
                    {
                        case DialogResult.OK:
                            messages.Visible = true;
                            //messages.Show();
                            messages.Update();
                            Process();
                            break;

                        case DialogResult.Cancel:
                            return;

                        default:
                        case DialogResult.None:
                            break;
                    }
                }
            }
        }

        static void Process()
        {
            int duplicatemessagekeycount = 0;
            int duplicatemessagecount = 0;
            int nomsgkey = 0;
            int noscope = 0;
            int nodescription = 0;
            int nodefaultmsg = 0;
            Char[] delimit = new Char[1];
            delimit[0] = ' ';


            add_message(false, "Load messages from previous drop");
            {
                String Filename = "";
                Filename = PrevMsgFolder + "\\ClientMessages.developer.translation";
                if (File.Exists(Filename))
                {
                    using (StreamReader sr = new StreamReader(Filename))
                    {
                        while (sr.Peek() >= 0)
                        {
                            InitMsg(PreviousMsgs, previous_messagecount);

                            if (LoadMessage(sr, PreviousMsgs, previous_messagecount))
                            {
                                previous_messagecount++;
                            }
                        }
                        sr.Close();
                    }
                }
                Filename = PrevMsgFolder + "\\ServerMessages.developer.translation";
                if (File.Exists(Filename))
                {
                    using (StreamReader sr = new StreamReader(Filename))
                    {
                        while (sr.Peek() >= 0)
                        {
                            InitMsg(PreviousMsgs, previous_messagecount);

                            if (LoadMessage(sr, PreviousMsgs, previous_messagecount))
                            {
                                previous_messagecount++;
                            }
                        }
                        sr.Close();
                    }
                }

                add_message(false, previous_messagecount.ToString() + " Previous Messages Loaded");
            }

            add_message(false, "Search for message files");
            {
                DirectoryInfo core_di = new DirectoryInfo(@CoreFolder);
                DirectoryInfo project_di = new DirectoryInfo(@ProjectFolder);

                FileInfo[] core_files = core_di.GetFiles("*.ms", SearchOption.AllDirectories);
                FileInfo[] project_files = project_di.GetFiles("*.ms", SearchOption.AllDirectories);
                String CoreCmpName;
                String ProjectCmpName;
                bool duplicate;
                String[] PrunedCoreFiles = new String[100000];
                int PrunedCoreFileCount = 0;
                String[] PrunedProjectFiles = new String[100000];
                int PrunedProjectFileCount = 0;
                int testfilecount = 0;
                int overriddenfilecount = 0;

                for (int ii = 0; ii < core_files.Length; ii++)
                {
                    duplicate = false;

                    if (core_files[ii].FullName.Contains("\\_"))
                    {
                        testfilecount++;
                    }
                    else
                    {
                        CoreCmpName = core_files[ii].FullName.Substring(CoreFolder.Length, core_files[ii].FullName.Length - CoreFolder.Length);

                        for (int jj = 0; jj < project_files.Length; jj++)
                        {
                            ProjectCmpName = project_files[jj].FullName.Substring(ProjectFolder.Length, project_files[jj].FullName.Length - ProjectFolder.Length);

                            if (CoreCmpName == ProjectCmpName)
                            {
                                duplicate = true;
                                break;
                            }
                        }

                        if (duplicate)
                        {
                            overriddenfilecount++;
                        }
                        else
                        {
                            PrunedCoreFiles[PrunedCoreFileCount] = core_files[ii].FullName;
                            PrunedCoreFileCount++;
                        }
                    }
                }


                for (int ii = 0; ii < project_files.Length; ii++)
                {
                    if (project_files[ii].FullName.Contains("\\_"))
                    {
                        testfilecount++;
                    }
                    else
                    {
                        PrunedProjectFiles[PrunedProjectFileCount] = project_files[ii].FullName;
                        PrunedProjectFileCount++;
                    }
                }


                add_message(false, "");
                add_message(false, "Pruned " + testfilecount.ToString() + " test files");
                add_message(false, "Pruned " + overriddenfilecount.ToString() + " overridden core files");
                add_message(false, "");

                add_message(false, PrunedCoreFileCount.ToString() + " core .ms files");
                add_message(false, PrunedProjectFileCount.ToString() + " project .ms files");
                add_message(false, "");


                add_message(false, "Loading Messages");

                for (int ii = 0; ii < PrunedCoreFileCount; ii++)
                {
                    bool ClientFile = IsClientFile(PrunedCoreFiles[ii]);

                    using (StreamReader sr = new StreamReader(PrunedCoreFiles[ii]))
                    {
                        while (sr.Peek() >= 0)
                        {
                            InitMsg(Msgs, messagecount);
                            Msgs[messagecount].FileName = PrunedCoreFiles[ii];

                            if (LoadMessage(sr, Msgs, messagecount))
                            {
                                Msgs[messagecount].CoreMsg = true;
                                Msgs[messagecount].ClientMsg = ClientFile;
                                if (IsMessageUpdated(messagecount))
                                    Msgs[messagecount].MsgUpdated = true;
                                AddScope(Msgs[messagecount].Scope);
                                messagecount++;
                            }
                        }
                        sr.Close();
                    }
                }

                for (int ii = 0; ii < PrunedProjectFileCount; ii++)
                {
                    bool ClientFile = IsClientFile(PrunedProjectFiles[ii]);

                    using (StreamReader sr = new StreamReader(PrunedProjectFiles[ii]))
                    {
                        while (sr.Peek() >= 0)
                        {
                            InitMsg(Msgs, messagecount);
                            Msgs[messagecount].FileName = PrunedProjectFiles[ii];

                            if (LoadMessage(sr, Msgs, messagecount))
                            {
                                Msgs[messagecount].ClientMsg = ClientFile;
                                if (IsMessageUpdated(messagecount))
                                    Msgs[messagecount].MsgUpdated = true;
                                AddScope(Msgs[messagecount].Scope);
                                messagecount++;
                            }
                        }
                        sr.Close();
                    }
                }

                add_message(false, "Message Loading completed");
                add_message(false, "");


                add_message(false, "Checking for duplicate message keys");
                for (int ii = 0; ii < messagecount - 1; ii++)
                {
                    if (Msgs[ii].DuplicateMsgKey)
                        continue;

                    for (int jj = ii + 1; jj < messagecount; jj++)
                    {
                        if (Msgs[jj].DuplicateMsgKey)
                            continue;

                        if (Msgs[ii].MessageKey == Msgs[jj].MessageKey)
                        {
                            Msgs[jj].DuplicateMsgKey = true;
                            duplicatemessagekeycount++;
                            add_message(false, "duplicate message key " + Msgs[jj].MessageKey + "  File: " + Msgs[jj].FileName);
                            //break;
                        }
                    }
                }


                add_message(false, "Checking for duplicate messages");
                for (int ii = 0; ii < messagecount - 1; ii++)
                {
                    if (Msgs[ii].DuplicateMsgKey)
                        continue;

                    if (Msgs[ii].DuplicateMsg)
                        continue;
             
                    for (int jj = ii + 1; jj < messagecount; jj++)
                    {
                        if (Msgs[jj].DuplicateMsgKey)
                            continue;

                        if (Msgs[jj].DuplicateMsg)
                            continue;

                        if (Msgs[ii].DefaultString == Msgs[jj].DefaultString)
                        {
                            Msgs[jj].DuplicateMsg = true;
                            Msgs[jj].DuplicateOfKey = Msgs[ii].MessageKey;
                            duplicatemessagecount++;
                            //break;
                        }
                    }
                }

                for (int ii = 0; ii < messagecount; ii++)
                {
                    if (Msgs[ii].DuplicateMsgKey)
                        continue;

                    if (Msgs[ii].MessageKey.Length == 0)
                        nomsgkey++;

                    if (Msgs[ii].Scope.Length == 0)
                        noscope++;

                    if (Msgs[ii].Description.Length == 0)
                        nodescription++;

                    if (Msgs[ii].DefaultString.Length == 0)
                        nodefaultmsg++;

                    totalmessagecount++;

                    if (Msgs[ii].ClientMsg)
                    {
                        clientmessagecount++;

                        if (Msgs[ii].CoreMsg)
                            coreclientmessagecount++;
                    }
                    else
                    {
                        servermessagecount++;

                        if (Msgs[ii].CoreMsg)
                            coreservermessagecount++;
                    }

                    if (!Msgs[ii].DuplicateMsg)
                    {
                        nodup_totalmessagecount++;

                        if (Msgs[ii].ClientMsg)
                        {
                            nodup_clientmessagecount++;

                            if (Msgs[ii].CoreMsg)
                                nodup_coreclientmessagecount++;
                        }
                        else
                        {
                            nodup_servermessagecount++;

                            if (Msgs[ii].CoreMsg)
                                nodup_coreservermessagecount++;
                        }
                    }

                    String tmpS = Msgs[ii].DefaultString;
                    tmpS = RemoveEscapes(tmpS);
                    tmpS = tmpS.Replace("{", " {");
                    tmpS = tmpS.Replace("}", "} ");
                    tmpS = tmpS.Replace('(', ' ');
                    tmpS = tmpS.Replace(')', ' ');
                    tmpS = tmpS.Trim();
                    String[] split_msg = tmpS.Split();

                    for (int jj = 0; jj < split_msg.Length; jj++)
                    {
                        if ( (split_msg[jj].Length == 0) ||
                             (!AnyAlpha(split_msg[jj])) ||
//                             ((split_msg[jj].Length == 1) && !IsAlpha(split_msg[jj])) ||
                             (split_msg[jj].StartsWith("{") && split_msg[jj].EndsWith("}")) )
                        {
                            //don't count any single character words
                            //don't count any words enclosed in {}
                        }
                        else
                        {
                            wordcount++;

                            if (!Msgs[ii].DuplicateMsg)
                                nodup_wordcount++;
                        }
                    }
                }

                add_message(false, messagecount.ToString() + " Loaded Messages");
                add_message(false, duplicatemessagekeycount.ToString() + " Messages w/ duplicated keys");
                add_message(false, duplicatemessagecount.ToString() + " Messages w/ duplicated default strings");
                add_message(false, "");

                add_message(false, totalmessagecount.ToString() + " Total Valid Messages");
                add_message(false, clientmessagecount.ToString() + " Client Messages");
                add_message(false, coreclientmessagecount.ToString() + " Core Client Messages");
                add_message(false, servermessagecount.ToString() + " Server Messages");
                add_message(false, coreservermessagecount.ToString() + " Core Server Messages");
                add_message(false, wordcount.ToString() + " Words");
                add_message(false, "");

                add_message(false, nodup_totalmessagecount.ToString() + " Total Valid Messages w/o dups");
                add_message(false, nodup_clientmessagecount.ToString() + " Client Messages w/o dups");
                add_message(false, nodup_coreclientmessagecount.ToString() + " Core Client Messages w/o dups");
                add_message(false, nodup_servermessagecount.ToString() + " Server Messages w/o dups");
                add_message(false, nodup_coreservermessagecount.ToString() + " Core Server Messages w/o dups");
                add_message(false, nodup_wordcount.ToString() + " Words w/o dups");
                add_message(false, "");

                add_message(false, nomsgkey.ToString() + " Messages w/ Blank MessageKey");
                add_message(false, noscope.ToString() + " Messages w/ Blank Scope");
                add_message(false, nodescription.ToString() + " Messages w/ Blank Description");
                add_message(false, nodefaultmsg.ToString() + " Messages w/ Blank DefaultString");
                add_message(false, "");

                add_message(false, scopecount.ToString() + " Scopes");
                for (int ii = 0; ii < scopecount; ii++)
                {
                    add_message(false, Scopes[ii].Scope + " " + Scopes[ii].count.ToString());
                }
                add_message(false, "");
                
                add_message(false, "Writing server message developer language file");
                String ServerFileName = NewMsgFolder + "\\ServerMessages.developer.translation";
                using (StreamWriter sw = new StreamWriter(ServerFileName, false, Encoding.UTF8))
                {
                    WriteBOM(sw);

                    for (int ii = 0; ii < messagecount; ii++)
                    {
                        if (Msgs[ii].DuplicateMsgKey)
                            continue;

                        if (bRemoveDuplicates && Msgs[ii].DuplicateMsg)
                            continue;

                        if (Msgs[ii].ClientMsg)
                            continue;

                        sw.WriteLine("Message");
                        sw.WriteLine("{");
                        sw.WriteLine("    MessageKey " + Msgs[ii].MessageKey);
                        if (Msgs[ii].Scope.Length > 0)
                            sw.WriteLine("    Scope " + Msgs[ii].Scope);
                        if (Msgs[ii].Description.Length > 0)
                            sw.WriteLine("    Description " + Msgs[ii].Description);

                        if (Msgs[ii].DefaultString.Length > 0)
                        {
                            sw.WriteLine("    DefaultString " + Msgs[ii].DefaultString);
                            sw.WriteLine("    TranslatedString " + Msgs[ii].DefaultString);
                        }
                        else
                        {
                            sw.WriteLine("    DefaultString <& &>");
                            sw.WriteLine("    TranslatedString <& &>");
                        }

                        //write out any flags
                        if (Msgs[ii].MsgUpdated)
                            sw.WriteLine("    Updated 1");
                        if (Msgs[ii].DuplicateMsg)
                            sw.WriteLine("    Duplicate " + Msgs[ii].DuplicateOfKey);
                        if (Msgs[ii].ClientMsg)
                            sw.WriteLine("    ClientMsg 1");
                        if (Msgs[ii].CoreMsg)
                            sw.WriteLine("    CoreMsg 1");
                        sw.WriteLine("    MsgFileName " + Msgs[ii].FileName);

                        sw.WriteLine("}");
                        sw.WriteLine("");
                    }
                }
                add_message(false, "Done writing file");
                add_message(false, "");

                add_message(false, "Writing client message developer language file");
                String ClientFileName = NewMsgFolder + "\\ClientMessages.developer.translation";
                using (StreamWriter sw = new StreamWriter(ClientFileName, false, Encoding.UTF8))
                {
                    WriteBOM(sw);

                    for (int ii = 0; ii < messagecount; ii++)
                    {
                        if (Msgs[ii].DuplicateMsgKey)
                            continue;

                        if (bRemoveDuplicates && Msgs[ii].DuplicateMsg)
                            continue;

                        if (!Msgs[ii].ClientMsg)
                            continue;

                        sw.WriteLine("Message");
                        sw.WriteLine("{");
                        sw.WriteLine("    MessageKey " + Msgs[ii].MessageKey);
                        if (Msgs[ii].Scope.Length > 0)
                            sw.WriteLine("    Scope " + Msgs[ii].Scope);
                        if (Msgs[ii].Description.Length > 0)
                            sw.WriteLine("    Description " + Msgs[ii].Description);

                        if (Msgs[ii].DefaultString.Length > 0)
                        {
                            sw.WriteLine("    DefaultString " + Msgs[ii].DefaultString);
                            sw.WriteLine("    TranslatedString " + Msgs[ii].DefaultString);
                        }
                        else
                        {
                            sw.WriteLine("    DefaultString <& &>");
                            sw.WriteLine("    TranslatedString <& &>");
                        }

                        //write out any flags
                        if (Msgs[ii].MsgUpdated)
                            sw.WriteLine("    Updated 1");
                        if (Msgs[ii].DuplicateMsg)
                            sw.WriteLine("    Duplicate " + Msgs[ii].DuplicateOfKey);
                        if (Msgs[ii].ClientMsg)
                            sw.WriteLine("    ClientMsg 1");
                        if (Msgs[ii].CoreMsg)
                            sw.WriteLine("    CoreMsg 1");
                        sw.WriteLine("    MsgFileName " + Msgs[ii].FileName);

                        sw.WriteLine("}");
                        sw.WriteLine("");
                    }
                }
                add_message(false, "Done writing file");
                add_message(false, "");


                if (bCreateDummyFiles)
                {
                    const int NUM_LANGUAGES = 7;
                    String[] Language = {"English",
                                                      "ChineseTraditional",
                                                      "Korean",
                                                      "Japanese",
                                                      "German",
                                                      "French",
                                                      "Spanish" };
                    String[] LanguageCode = {"EN",
                                                          "ZH",
                                                          "KO",
                                                          "JA",
                                                          "DE",
                                                          "FR",
                                                          "ES" };

                    for (int lang = 0; lang < NUM_LANGUAGES; lang++)
                    {
                        add_message(false, "Writing server message " + Language[lang] + " language file");
                        ServerFileName = NewMsgFolder + "\\ServerMessages." + Language[lang] + ".translation";
                        using (StreamWriter sw = new StreamWriter(ServerFileName, false, Encoding.UTF8))
                        {
                            WriteBOM(sw);

                            for (int ii = 0; ii < messagecount; ii++)
                            {
                                if (Msgs[ii].DuplicateMsgKey)
                                    continue;

                                if (bRemoveDuplicates && Msgs[ii].DuplicateMsg)
                                    continue;

                                if (Msgs[ii].ClientMsg)
                                    continue;

                                sw.WriteLine("Message");
                                sw.WriteLine("{");
                                sw.WriteLine("    MessageKey " + Msgs[ii].MessageKey);
                                if (Msgs[ii].Scope.Length > 0)
                                    sw.WriteLine("    Scope " + Msgs[ii].Scope);
                                if (Msgs[ii].Description.Length > 0)
                                    sw.WriteLine("    Description " + Msgs[ii].Description);

                                if (Msgs[ii].DefaultString.Length > 0)
                                {
                                    sw.WriteLine("    DefaultString " + Msgs[ii].DefaultString);

                                    String DummyTrans = RemoveEscapes(Msgs[ii].DefaultString);
                                    DummyTrans = "[" + LanguageCode[lang] + " " + DummyTrans + "]";
                                    DummyTrans = AddEscapes(DummyTrans);
                                    sw.WriteLine("    TranslatedString " + DummyTrans);
                                }
                                else
                                {
                                    sw.WriteLine("    DefaultString <& &>");
                                    sw.WriteLine("    TranslatedString <& &>");
                                }

                                //write out any flags
                                if (Msgs[ii].MsgUpdated)
                                    sw.WriteLine("    Updated 1");
                                if (Msgs[ii].DuplicateMsg)
                                    sw.WriteLine("    Duplicate " + Msgs[ii].DuplicateOfKey);
                                if (Msgs[ii].ClientMsg)
                                    sw.WriteLine("    ClientMsg 1");
                                if (Msgs[ii].CoreMsg)
                                    sw.WriteLine("    CoreMsg 1");
                                sw.WriteLine("    MsgFileName " + Msgs[ii].FileName);

                                sw.WriteLine("}");
                                sw.WriteLine("");
                            }
                        }
                        add_message(false, "Done writing file");
                        add_message(false, "");

                        add_message(false, "Writing client message " + Language[lang] + " language file");
                        ClientFileName = NewMsgFolder + "\\ClientMessages." + Language[lang] + ".translation";
                        using (StreamWriter sw = new StreamWriter(ClientFileName, false, Encoding.UTF8))
                        {
                            WriteBOM(sw);

                            for (int ii = 0; ii < messagecount; ii++)
                            {
                                if (Msgs[ii].DuplicateMsgKey)
                                    continue;

                                if (bRemoveDuplicates && Msgs[ii].DuplicateMsg)
                                    continue;

                                if (!Msgs[ii].ClientMsg)
                                    continue;

                                sw.WriteLine("Message");
                                sw.WriteLine("{");
                                sw.WriteLine("    MessageKey " + Msgs[ii].MessageKey);
                                if (Msgs[ii].Scope.Length > 0)
                                    sw.WriteLine("    Scope " + Msgs[ii].Scope);
                                if (Msgs[ii].Description.Length > 0)
                                    sw.WriteLine("    Description " + Msgs[ii].Description);

                                if (Msgs[ii].DefaultString.Length > 0)
                                {
                                    sw.WriteLine("    DefaultString " + Msgs[ii].DefaultString);

                                    String DummyTrans = RemoveEscapes(Msgs[ii].DefaultString);
                                    DummyTrans = "[" + LanguageCode[lang] + " " + DummyTrans + "]";
                                    DummyTrans = AddEscapes(DummyTrans);
                                    sw.WriteLine("    TranslatedString " + DummyTrans);

                                }
                                else
                                {
                                    sw.WriteLine("    DefaultString <& &>");
                                    sw.WriteLine("    TranslatedString <& &>");
                                }

                                //write out any flags
                                if (Msgs[ii].MsgUpdated)
                                    sw.WriteLine("    Updated 1");
                                if (Msgs[ii].DuplicateMsg)
                                    sw.WriteLine("    Duplicate " + Msgs[ii].DuplicateOfKey);
                                if (Msgs[ii].ClientMsg)
                                    sw.WriteLine("    ClientMsg 1");
                                if (Msgs[ii].CoreMsg)
                                    sw.WriteLine("    CoreMsg 1");
                                sw.WriteLine("    MsgFileName " + Msgs[ii].FileName);

                                sw.WriteLine("}");
                                sw.WriteLine("");
                            }
                        }
                        add_message(false, "Done writing file");
                        add_message(false, "");
                    }
                }

                using (StreamWriter sw = new StreamWriter(NewMsgFolder + "\\MessagePacker.log"))
                {
                    for (int ii = 0; ii < messages.listBox1.Items.Count; ii++)
                    {
                        sw.WriteLine(messages.listBox1.Items[ii].ToString());
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
                //messages.ShowDialog();
            }
        }

        static void add_message(bool error, String msg)
        {
            if (error) error_flag = true;

            messages.listBox1.Items.Insert(messages.listBox1.Items.Count, msg);
            //messages.Show();
            messages.Update();
        }

        static bool EscapedString(String str)
        {
            if ( (str.StartsWith("<&") &&
                  str.EndsWith("&>")) ||
                  (str.StartsWith("<<") &&
                  str.EndsWith(">>")) )
                return true;

            return false;
        }

        static bool QuotedString(String str)
        {
            if (str.StartsWith("\"") &&
                 str.EndsWith("\""))
                return true;

            return false;
        }

        static String RemoveEscapes(String str)
        {
            String retstr = str;

            if (EscapedString(retstr))
            {
                retstr = retstr.Substring(2, retstr.Length - 4);
            }

            return retstr;
        }

        static String RemoveQuotes(String str)
        {
            String retstr = str;

            if (QuotedString(retstr))
            {
                retstr = retstr.Substring(1, retstr.Length - 2);
            }

            return retstr;
        }

        static String AddEscapes(String str)
        {
            String retstr = str;

            if (!EscapedString(retstr))
            {
                retstr = "<&" + retstr + "&>";
            }

            return retstr;
        }
            
        static bool LoadMessage(StreamReader sr, Msg[] Msgs, int idx)
        {
            Char[] trimchar = new Char[1];
            trimchar[0] = '"';

            if (idx >= MAX_MESSAGES)
            {
               add_message(true, "Too many messages");
               return false ;
            }

            for (;;)
            {
                if (sr.Peek() < 0)
                    return false;
                
                String line = sr.ReadLine().Trim();
                String lcline = line.ToLower();
            
                if (lcline.Equals("message"))
                {
                    //InitMsg(Msgs, idx);

                    for (; ; )
                    {
                        if (sr.Peek() < 0)
                            return false;

                        line = sr.ReadLine().Trim();
                        lcline = line.ToLower();

                        if (lcline.StartsWith("}"))
                        {
                            return true;
                        }

                        if (lcline.StartsWith("messagekey"))
                        {
                            line = lcline;
                            line = line.Substring("messagekey".Length, line.Length - "messagekey".Length);
                            line = line.Trim();

                            line = RemoveEscapes(line);
                            line = line.Trim();
                            line = RemoveQuotes(line);
                            line = line.Trim();
                            line = AddEscapes(line);

                            Msgs[idx].MessageKey = line;
                        }

                        if (lcline.StartsWith("scope"))
                        {
                            line = line.Substring("scope".Length, line.Length - "scope".Length);
                            line = line.Trim();

                            line = RemoveEscapes(line);
                            line = line.Trim();
                            line = RemoveQuotes(line);
                            line = line.Trim();
                            line = AddEscapes(line);

                            Msgs[idx].Scope = line;
                        }

                        if (lcline.StartsWith("description"))
                        {
                            line = line.Substring("description".Length, line.Length - "description".Length);
                            line = line.Trim();
                            if ((line.StartsWith("<<") || line.StartsWith("<&")) && !EscapedString(line))
                            {
                                while (!(line.EndsWith("&>") || line.EndsWith(">>")))
                                    line += sr.ReadLine().Trim();
                            }
                            else
                            {
                                line = RemoveEscapes(line);
                                line = line.Trim();
                                line = RemoveQuotes(line);
                                line = line.Trim();
                                line = AddEscapes(line);
                            }
                            Msgs[idx].Description = line.Trim();
                        }

                        if (lcline.StartsWith("defaultstring"))
                        {
                            line = line.Substring("defaultstring".Length, line.Length - "defaultstring".Length);
                            line = line.Trim();
                            if ((line.StartsWith("<<") || line.StartsWith("<&")) && !EscapedString(line))
                            {
                                while (!(line.EndsWith("&>") || line.EndsWith(">>")))
                                    line += sr.ReadLine().Trim();
                                
                            }
                            else
                            {
                                line = RemoveEscapes(line);
                                line = line.Trim();
                                line = RemoveQuotes(line);
                                line = line.Trim();
                                line = AddEscapes(line);
                            }
                            Msgs[idx].DefaultString = line.Trim();
                        }
                    }
                }
            }
        }

        static bool IsClientFile(String filename)
        {
            for (int ii=0; ii<ClientFolderCount; ii++)
            {
                if (filename.StartsWith(ClientFolders[ii]))
                    return true;
            }

            return false;
        }

        
        static bool IsMessageUpdated(int idx)
        {
            for (int ii = 0; ii < previous_messagecount; ii++)
            {
                if (Msgs[idx].MessageKey == PreviousMsgs[ii].MessageKey)
                {
                    if (Msgs[idx].DefaultString == PreviousMsgs[ii].DefaultString)
                        return false;
                    else
                        return true;
                }
            }

            return true;
        }

        static int FindScope(String scope)
        {
            for (int ii = 0; ii < scopecount; ii++)
            {
                if (Scopes[ii].Scope == scope)
                {
                    return ii;
                }
            }

            return -1;
        }

        static void AddScope(String scope)
        {
            int idx = FindScope(scope);

            if (idx == -1)
            {
                Scopes[scopecount].Scope = scope;
                Scopes[scopecount].count = 1;
                scopecount++;
            }
            else
            {
                Scopes[idx].count++;
            }
        }


        static void WriteBOM(StreamWriter sw)
        {
//            char[] tmp = { '\xEF', '\xBB', '\xBF' };
//            sw.Write(tmp, 0, 3);
        }

        static bool IsAlpha(string strToCheck)
        {
            Regex objAlphaPattern = new Regex("[^a-zA-Z]");

            return !objAlphaPattern.IsMatch(strToCheck);
        }

        static bool AnyAlpha(string strToCheck)
        {
            Regex objAlphaPattern = new Regex("[a-zA-Z]");

            return objAlphaPattern.IsMatch(strToCheck);
        }
    }
}