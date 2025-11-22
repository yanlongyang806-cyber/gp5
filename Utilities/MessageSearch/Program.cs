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
    static class Program
    {

        public static Form1 messages = new Form1();

        struct Msg
        {
            public String MessageKey;
            public String Scope;
            public String Description;
            public String DefaultString;
            public String FileName;
        }

        const int MAX_MESSAGES = 64000;

        static Msg[] Msgs = new Msg[MAX_MESSAGES];
        static int messagecount = 0;

        static void InitMsg(Msg[] Msgs, int idx)
        {
            Msgs[idx].MessageKey = "";
            Msgs[idx].Scope = "";
            Msgs[idx].Description = "";
            Msgs[idx].DefaultString = "";
            Msgs[idx].FileName = "";
        }

        public static String[] SearchStrings = new String[128];
        public static int SearchStringCount = 0;

        [STAThread]
        static void Main()
        {
            object tmpObj = new object();
            Char[] folder_delimit = new Char[1];
            folder_delimit[0] = ';';

            messages.Show();
            messages.Update();
            add_message(false, "Loading Messages...");
            LoadMessages();
            add_message(false, "Ready to Search Messages.");
            add_message(false, "");
            add_message(false, "");

            for (; ; )
            {
                messages.Visible = false;
                DialogResult res = messages.ShowDialog();

                switch (res)
                {
                case DialogResult.Cancel:
                    return;
                }
            }
        }

        [STAThread]
        public static void LoadMessages()
        {
            Char[] delimit = new Char[1];
            delimit[0] = ' ';

            
            {
                DirectoryInfo project_dir1 = new DirectoryInfo(@"c:\\fightclub\\data\\defs");
                DirectoryInfo project_dir2 = new DirectoryInfo(@"c:\\fightclub\\data\\ui");
                DirectoryInfo project_dir3 = new DirectoryInfo(@"c:\\fightclub\\data\\messages");

                FileInfo[] project_files1 = project_dir1.GetFiles("*.ms", SearchOption.AllDirectories);
                FileInfo[] project_files2 = project_dir2.GetFiles("*.ms", SearchOption.AllDirectories);
                FileInfo[] project_files3 = project_dir3.GetFiles("*.ms", SearchOption.AllDirectories);

                for (int ii = 0; ii < project_files1.Length; ii++)
                {
                    using (StreamReader sr = new StreamReader(project_files1[ii].FullName))
                    {
                        while (sr.Peek() >= 0)
                        {
                            InitMsg(Msgs, messagecount);
                            Msgs[messagecount].FileName = project_files1[ii].FullName;
                            
                            if (LoadMessage(sr, Msgs, messagecount))
                            {
                            }
                        }
                        sr.Close();
                    }
                }

                for (int ii = 0; ii < project_files2.Length; ii++)
                {
                    using (StreamReader sr = new StreamReader(project_files2[ii].FullName))
                    {
                        while (sr.Peek() >= 0)
                        {
                            InitMsg(Msgs, messagecount);
                            Msgs[messagecount].FileName = project_files2[ii].FullName;

                            if (LoadMessage(sr, Msgs, messagecount))
                            {
                            }
                        }
                        sr.Close();
                    }
                }

                for (int ii = 0; ii < project_files3.Length; ii++)
                {
                    using (StreamReader sr = new StreamReader(project_files3[ii].FullName))
                    {
                        while (sr.Peek() >= 0)
                        {
                            InitMsg(Msgs, messagecount);
                            Msgs[messagecount].FileName = project_files3[ii].FullName;

                            if (LoadMessage(sr, Msgs, messagecount))
                            {
                            }
                        }
                        sr.Close();
                    }
                }
            
            }
        }


        [STAThread]
        public static void SearchMessages()
        {
            String SearchString = messages.textBox1.Text;

            for (int ii = 0; ii < messagecount; ii++)
            {
                if (Msgs[ii].DefaultString.Contains(SearchString))
                {
                    add_message(false, RemoveEscapes(Msgs[ii].MessageKey));
                }
            }

            add_message(false, "");
            add_message(false, "");
        }


        static void add_message(bool error, String msg)
        {
            messages.listBox1.Items.Insert(messages.listBox1.Items.Count, msg);
            //messages.Show();
            messages.Update();
        }

        static bool EscapedString(String str)
        {
            if (str.StartsWith("<&") &&
                 str.EndsWith("&>"))
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
                            messagecount++;
                            return true;
                        }

                        if (lcline.StartsWith("messagekey"))
                        {
                            line = line.Substring("messagekey".Length, line.Length - "messagekey".Length);
                            line = line.Trim();
                            line = line.TrimStart(trimchar);
                            line = line.TrimEnd(trimchar);
                            line = line.Trim();
                            line = line.ToLower();

                            line = RemoveQuotes(line);
                            line = AddEscapes(line);
                            Msgs[idx].MessageKey = line;
                        }

                        if (lcline.StartsWith("scope"))
                        {
                            line = line.Substring("scope".Length, line.Length - "scope".Length);
                            line = line.Trim();

                            line = RemoveQuotes(line);
                            line = AddEscapes(line);
                            Msgs[idx].Scope = line;
                        }

                        if (lcline.StartsWith("description"))
                        {
                            line = line.Substring("description".Length, line.Length - "description".Length);
                            line = line.Trim();

                            line = RemoveQuotes(line);
                            line = AddEscapes(line);
                            Msgs[idx].Description = line;
                        }

                        if (lcline.StartsWith("defaultstring"))
                        {
                            line = line.Substring("defaultstring".Length, line.Length - "defaultstring".Length);
                            line = line.Trim();
                            line = line.TrimStart(trimchar);
                            line = line.TrimEnd(trimchar);

                            line = RemoveQuotes(line);
                            line = AddEscapes(line);
                            Msgs[idx].DefaultString = line;
                        }
                    }
                }
            }
        }
    }
}