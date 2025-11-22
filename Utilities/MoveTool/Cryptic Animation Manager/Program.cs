using System;
using System.Collections.Generic;
using System.Windows.Forms;
using System.IO;
using System.Collections;

namespace Cryptic_Animation_Manager
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
            Application.Run(new frmAnimManager());
        }

    }

    public class DynSequence
    {
        public string name;
        public string file;
        public ArrayList bits;
        public ArrayList optionalBits;
        public ArrayList moves;
        public int priority;
        public int start;
        public int length;

        public DynSequence()
        {

            this.bits = new ArrayList();
            this.moves = new ArrayList();
            this.optionalBits = new ArrayList();

        }
    }

    public class DynMove
    {
        public string name;
        public string file;
        public ArrayList danim;
        public int start;
        public int length;

        public DynMove()
        {

            this.danim = new ArrayList();

        }
    }
}