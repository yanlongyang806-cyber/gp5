using System;
using System.Collections.Generic;
using System.Text;
using System.IO;
using System.Diagnostics;

namespace DirectorySyms
{
    class Program
    {
        static void Main(string[] args)
        {
            bool verbose = true;
            bool execute = true;
            Process proc = new Process();

            if (args.Length > 0)
            {
                if ( Directory.Exists(args[0]) )
                {
                    String cmd = "C:\\Core\\tools\\bin\\symstorevs8.exe";
                    String cmdarg = "";

                    if ( verbose ) Console.WriteLine("");
                    if (verbose) Console.WriteLine("");

                    String[] exefiles = Directory.GetFiles(args[0], "*.exe");
                    if (verbose) Console.WriteLine(args[0] + " contains " + exefiles.Length.ToString() + " .exe files.");
                    for (int ii = 0; ii < exefiles.Length; ii++)
                    {
                        cmdarg = "add /compress /f " + exefiles[ii] + " /s N:\\nobackup\\symserv\\datavs8 /t \"Cryptic Programs\"";
                        if (verbose) Console.WriteLine(cmdarg);
                        if (execute)
                        {
                            proc.StartInfo.FileName = cmd;
                            proc.StartInfo.Arguments = cmdarg;
                            proc.Start();
                            while (!proc.HasExited) ;
                        }
                    }
                    if (verbose) Console.WriteLine("");

                    String[] dllfiles = Directory.GetFiles(args[0],"*.dll");
                    if (verbose) Console.WriteLine(args[0] + " contains " + dllfiles.Length.ToString() + " .dll files.");
                    for (int ii = 0; ii < dllfiles.Length; ii++)
                    {
                        cmdarg = "add /compress /f " + dllfiles[ii] + " /s N:\\nobackup\\symserv\\datavs8 /t \"Cryptic Programs\"";
                        if (verbose) Console.WriteLine(cmdarg);
                        if (execute)
                        {
                            proc.StartInfo.FileName = cmd;
                            proc.StartInfo.Arguments = cmdarg;
                            proc.Start();
                            while (!proc.HasExited) ;
                        }
                    }
                    if (verbose) Console.WriteLine("");

                    String[] pdbfiles = Directory.GetFiles(args[0],"*.pdb");
                    if (verbose) Console.WriteLine(args[0] + " contains " + pdbfiles.Length.ToString() + " .pdb files.");
                    for (int ii = 0; ii < pdbfiles.Length; ii++)
                    {
                        cmdarg = "add /compress /f " + pdbfiles[ii] + " /s N:\\nobackup\\symserv\\datavs8 /t \"Cryptic Programs\"";
                        if (verbose) Console.WriteLine(cmdarg);
                        if (execute)
                        {
                            proc.StartInfo.FileName = cmd;
                            proc.StartInfo.Arguments = cmdarg;
                            proc.Start();
                            while (!proc.HasExited) ;
                        }
                    }
                }
                else
                {
                    Console.WriteLine("Directory " + args[0] + " does not exist.");
                }
            }
        }
    }
}
