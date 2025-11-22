using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using System.Net.Mail;

namespace watchdog
{
    class Program
    {
        static void Main(string[] args)
        {
            bool running = true;
            DateTime stop_time = DateTime.Now;
            String ExePath = "";
            String ProcessName = "";
            String Email = "";
            int loop_time = 0;
            int dead_time = 0;

            if (!File.Exists("watchdog.config"))
                return;

            using (StreamReader sr = new StreamReader("watchdog.config") )
            {
                String line1 = sr.ReadLine();
                String line2 = sr.ReadLine();
                String line3 = sr.ReadLine();
                String line4 = sr.ReadLine();

                ExePath = line1.Trim();
                ProcessName = Path.GetFileNameWithoutExtension(ExePath);
                Email = line2.Trim();
                loop_time = int.Parse(line3.Trim());
                dead_time = int.Parse(line4.Trim());
            }

            for (; ; )
            {
                Process[] procs = Process.GetProcessesByName(ProcessName);

                if (procs.Length == 0)
                {
                    //proc is not running

                    if ( running )
                    {
                        //just noticed that proc is not running
                        running = false;

                        stop_time = DateTime.Now;
                    }
                    else
                    {
                        //time how long we noticed that proc has been stopped
                        DateTime currentTime = DateTime.Now;

                        TimeSpan duration = currentTime - stop_time;

                        //if stopped for longer than allowed period then restart it (5 minutes)
                        if (duration.TotalSeconds >= dead_time)
                        {
                            Process.Start(ExePath);

                            using (StreamWriter sw = File.AppendText("watchdog.log") )
                            {
                                sw.WriteLine("{0} restarted {1}", currentTime.ToString(), ExePath);
                            }

                            {
                                MailMessage message = new MailMessage( System.Environment.MachineName + "@crypticstudios.com",
                                                                       Email,
                                                                       "restarted " + ProcessName + " on " + System.Environment.MachineName + " at " + currentTime.ToString(),
                                                                       "");

                                SmtpClient client = new SmtpClient("galaxy.paragon.crypticstudios.com");
                                client.Send(message);
                            }
                        }
                    }
                }
                else
                {
                    //at least 1 sentry proc is running
                    running = true;
                }

                //wait for 1 minute
                System.Threading.Thread.Sleep(loop_time);
            }
        }
    }
}
