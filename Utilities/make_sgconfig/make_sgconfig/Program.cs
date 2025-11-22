using System;
using System.Collections.Generic;
using System.Windows.Forms;
using System.Data;
using System.Text;
using System.IO;

namespace make_sgconfig
{
    static class Program
    {
        /// <summary>
        /// The main entry point for the application.
        /// </summary>
        [STAThread]
        static void Main()
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            // create and show an open file dialog
            OpenFileDialog dlgdest = new OpenFileDialog();
            dlgdest.Title = "Choose destination file";
            dlgdest.CheckFileExists = false;
            dlgdest.DefaultExt = "ini";
            dlgdest.Filter = "Config files (*.ini)|*.ini";
            //dlgdest.InitialDirectory = "C:\\";
            dlgdest.FileName = "sgconfig.ini";
            if (dlgdest.ShowDialog() != DialogResult.OK)
            {
                return;
            }

            // create and show an open file dialog
            OpenFileDialog dlgsrc = new OpenFileDialog();
            dlgsrc.Title = "Choose header file to include";
            dlgsrc.CheckFileExists = false;
            dlgsrc.DefaultExt = "txt";
            dlgsrc.Filter = "Config files (*.txt)|*.txt";
            //dlgsrc.InitialDirectory = "C:\\";
            dlgsrc.FileName = "sgconfig_header.txt";
            if (dlgsrc.ShowDialog() != DialogResult.OK)
            {
                return;
            }

            
            using (StreamWriter sw = new StreamWriter(dlgdest.FileName))
            {
                using (StreamReader sr = new StreamReader(dlgsrc.FileName))
                {
                    string line;

                    while ((line = sr.ReadLine()) != null)
                    {
                        sw.WriteLine(line);
                    }
                    sr.Close();
                }
     
                String ip_prefix = "192.168.195.";
                UInt32 lsp_port = 0;
                UInt32  base_port = 7000;

                //full range is 1 through 254, for now 211 through 240
                for (UInt32 octet=211; octet<=240; octet++)
                {
                    for (UInt32 port = 0; port <= 255; port++)
                    {
                        lsp_port = (octet << 8) + port;

                        sw.WriteLine("Server");
                        sw.WriteLine("{");
                        sw.WriteLine("    Id                  {0}   ;0x{1}", lsp_port.ToString(), lsp_port.ToString("X"));
                        sw.WriteLine("    Service             FightClub");
                        sw.WriteLine("    Address             {{ InterfaceId 1 Ip {0}{1} Port {2} }}", ip_prefix, octet.ToString(), base_port + port);
                        sw.WriteLine("}");
                        sw.WriteLine("");
                    }
                }

                sw.Close();
            }
        }
    }
}