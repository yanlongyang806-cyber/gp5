using System;
using System.Collections.Generic;
using System.Text;
using System.IO;

namespace newdel
{
    class Program
    {
        static void Main(string[] args)
        {
            if (args.Length != 2)
            {
                System.Console.Write("usage: newdel start_path search_pattern\n");
                return;
            }

            if (Directory.Exists(args[0]))
            {
                DeleteItem(args[0], args[1]);
            }
        }

        static void DeleteItem(String itemname, String pattern)
        {
                //loop for all files
                string[] entries = Directory.GetFiles(itemname, pattern);
                foreach (string name in entries)
                {
                    File.SetAttributes(name, 0);

                    System.Console.Write("Deleted File {0}\n", name);
                    File.Delete(name);
                }

                //loop for all directories
                entries = Directory.GetDirectories(itemname, pattern);
                foreach (string name in entries)
                {
                    DeleteItem(name, "*");

                    File.SetAttributes(name, 0);

                    System.Console.Write("Deleted Dir {0}\n", name);
                    Directory.Delete(name, true);
                }
       }
    }
}
