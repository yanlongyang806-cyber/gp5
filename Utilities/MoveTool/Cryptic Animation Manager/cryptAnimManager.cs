using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Text;
using System.Windows.Forms;
using System.IO;
using System.Collections;
using System.Text.RegularExpressions;

namespace Cryptic_Animation_Manager
{
    public partial class frmAnimManager : Form
    {
        string pathAnimList = "\\data\\ai\\animlists\\";
        string pathPowerArt = "\\data\\powerart\\";
        string pathSeqBits = "\\data\\dyn\\seqbits\\";
        string[] projSeqFiles;
        string[] coreSeqFiles;
        string[] projMoveFiles;
        string[] coreMoveFiles;
        string[] powerartFiles;

        ArrayList pathProjects = new ArrayList();
        ArrayList animList = new ArrayList();
        ArrayList dynSeqs = new ArrayList();
        ArrayList dynMoves = new ArrayList();
        ArrayList coreDynSeqs = new ArrayList();
        ArrayList coreDynMoves = new ArrayList();
        ArrayList coreSequencers = new ArrayList();
        ArrayList Sequencers = new ArrayList();
        ArrayList curSequencer = new ArrayList();
        
        public frmAnimManager()
        {
            InitializeComponent();

            loadProjects();

            comboProject.SelectedIndex = 0;

            loadAnimList(comboProject.SelectedIndex);

            listAnims.SelectedIndex = 0;

            coreMoveFiles = findFiles(pathProjects[0] + "\\data\\dyn\\move\\", "*.move", true);

            comboProject.SelectedIndex = 0;

            comboTarget.SelectedIndex  = 0;

        }

        private void loadProjects()
        {

            StreamReader readerProject = new StreamReader("projects.txt");

            try
            {
                do
                {
                    string tmpData = readerProject.ReadLine();
                    string[] projectData = tmpData.Split(',');

                    comboProject.Items.Add(projectData[0]);
                    if(!projectData[0].Equals("Core")) comboTarget.Items.Add(projectData[0]);
                    pathProjects.Add(projectData[1]);

                }
                while (readerProject.Peek() != -1);
            }

            catch
            {

                // End of File

            }

            finally
            {

                readerProject.Close();

            }

        }

        // Function to find and load the anim list files. Should be altered to use the find-file function to load any animlist files
        // but for now only loads the two specific ones.
        private void loadAnimList(int projectID)
        {
            string path;

            animList.Clear();
            listAnims.Items.Clear();

            string[] filelist = findFiles(pathProjects[projectID] + pathAnimList, "*.al", false);

            path = filelist[filelist.Length - 1];

            animList = loadAnimListFile(path);

            foreach (string[] a in animList)
            {

                listAnims.Items.Add(a[0]);

            }

            loadSequencers(projectID);

        }

        public void loadSequencers(int projectID)
        {

            comboSequencer.Items.Clear();

            // Load sequencer and move file arrays

            Sequencers = findSeqs(pathProjects[projectID] + "\\data\\dyn\\sequence\\");
            coreSequencers = findSeqs(pathProjects[0] + "\\data\\dyn\\sequence\\");

            //projMoveFiles = findMoveFiles(pathProjects[projectID] + "\\data\\dyn\\move\\");
            //coreMoveFiles = findMoveFiles(pathProjects[0] + "\\data\\dyn\\move\\");

            foreach (ArrayList s in Sequencers)
            {

                comboSequencer.Items.Add((string)s[0]);

            }
            foreach (ArrayList s in coreSequencers)
            {

                comboSequencer.Items.Add((string)s[0]);

            }
            comboSequencer.SelectedIndex = 0;


            if (comboSequencer.SelectedIndex < Sequencers.Count)
            {
                curSequencer = (ArrayList)Sequencers[comboSequencer.SelectedIndex];
            }
            else
            {

                curSequencer = (ArrayList)coreSequencers[comboSequencer.SelectedIndex - Sequencers.Count];

            }

            dynSeqs = loadSeqs((string[])curSequencer[1]);

            projMoveFiles = findFiles(pathProjects[projectID] + "\\data\\dyn\\move\\", "*.move", true);
            coreMoveFiles = findFiles(pathProjects[0] + "\\data\\dyn\\move\\", "*.move", true);

            dynMoves = loadMoves(projMoveFiles);
            coreDynMoves = loadMoves(coreMoveFiles);

        }

        // Really hacky parsing right now... had to do a quick fix

        public ArrayList loadAnimListFile(string p)
        {

            StreamReader readerAnimList = new StreamReader(p);

            ArrayList anims = new ArrayList();

            try
            {
                do
                {
                    char[] delimiters = { ' ', '\t' };
                    string tmpData = readerAnimList.ReadLine();
                    string[] tokens = tmpData.Split(delimiters);

                    ArrayList tokensClean = new ArrayList();

                    foreach (string t in tokens)
                    {
                        if (t.Contains("/") || t.Contains("#")) break;
                        if (!t.Equals(String.Empty))
                        {
                            tokensClean.Add(t);
                        }

                    }
                    string firstToken = "";
                    if (tokensClean.Count > 0) firstToken = tokensClean[0].ToString();

                    if (firstToken.Equals("AIAnimList"))
                    {
                        string bits = "";
                        string lastToken = "";

                        while (lastToken != "}")
                        {
                            tmpData = readerAnimList.ReadLine();
                            tokens = tmpData.Split(delimiters);

                            ArrayList tokensClean2 = new ArrayList();

                            foreach (string t in tokens)
                            {
                                if (t.Contains("/") || t.Contains("#")) break;
                                if (!t.Equals(String.Empty))
                                {
                                    tokensClean2.Add(t);
                                }

                            }
                            if (tokensClean2.Count > 0) firstToken = tokensClean2[0].ToString();
                            lastToken = firstToken;
                            if (firstToken.Equals("Bit"))
                            {

                                for (int t = 1; t < tokensClean2.Count; t++)
                                {

                                    bits += tokensClean2[t] + " ";

                                }

                            }
                        }
                        string[] animEntry = new string[2] { (string)tokensClean[1], bits };
                        anims.Add(animEntry);

                    }


                }
                while (readerAnimList.Peek() != -1);
            }

            catch
            {

                // End of File

            }

            finally
            {

                readerAnimList.Close();

            }
            return anims;

        }

        public void loadPowerArts(int projectID)
        {

            animList.Clear();
            listAnims.Items.Clear();

            loadSequencers(projectID);

            ArrayList tmppa = new ArrayList();

            //Add support for txt includes later...
            if (Directory.Exists(pathProjects[projectID] + pathPowerArt))
            {
                string[] files = findFiles(pathProjects[projectID] + pathPowerArt, "*.powerart", false);
                foreach (string f in files)
                {
                    tmppa = loadPowerArtFile(f);
                    foreach (string[] a in tmppa)
                    {
                        animList.Add(a);
                    }
                }

                foreach (string[] a in animList)
                {

                    listAnims.Items.Add(a[0]);

                }
            }else{
                string[] nullanim = { "No PowerArt Files", "NA" };
                animList.Add(nullanim);
                listAnims.Items.Add("No PowerArt Files");
            }
        }

        public ArrayList loadPowerArtFile(string p)
        {
            // Will need to be added too to strip more bits. For now, focusing on standard bits (Activate, ActivateSticky, ChargeBits and such)
            StreamReader readerPA = new StreamReader(p);
            ArrayList anims = new ArrayList(); //Format should be Name, BITS;
            
            do
            {

                string line = readerPA.ReadLine();
                // Remove leading and trailing whitespace and comments
                line = Regex.Replace(line, @"[#|//].*", "");
                line = Regex.Replace(line, @"^\s*", "");
                line = Regex.Replace(line, @"\s*$", "");

                string[] tokens = Regex.Split(line, @"\s+");
                string[] tmparray = { "", "" };
                string[] filename = p.Split('\\');
                string bits = "";

                if (tokens[0].Equals("ActivateBits"))
                {

                    tmparray[0] = filename[filename.Length - 1] + " Activate";
                    for(int b = 1; b < tokens.Length; b++)
                    {
                        if (b != 1) bits = bits + " ";
                        bits = bits + tokens[b];

                    }
                    tmparray[1] = bits;
                    anims.Add(tmparray);

                }
                else if (tokens[0].Equals("ActivateStickyBits"))
                {

                    tmparray[0] = filename[filename.Length - 1] + " ActivateSticky";
                    for (int b = 1; b < tokens.Length; b++)
                    {
                        if (b != 1) bits = bits + " ";
                        bits = bits + tokens[b];

                    }
                    tmparray[1] = bits;
                    anims.Add(tmparray);

                }
                else if (tokens[0].Equals("ChargeBits") || tokens[0].Equals("ChargeStickyBits"))
                {

                    tmparray[0] = filename[filename.Length - 1] + " Charge";
                    for (int b = 1; b < tokens.Length; b++)
                    {
                        if (b != 1) bits = bits + " ";
                        bits = bits + tokens[b];

                    }
                    tmparray[1] = bits;
                    anims.Add(tmparray);

                }
                else if (tokens[0].Equals("LungeStickyBits"))
                {

                    tmparray[0] = filename[filename.Length - 1] + " LungeSticky";
                    for (int b = 1; b < tokens.Length; b++)
                    {
                        if (b != 1) bits = bits + " ";
                        bits = bits + tokens[b];

                    }
                    tmparray[1] = bits;
                    anims.Add(tmparray);

                }

            } while (readerPA.Peek() != -1);

            readerPA.Close();

            return anims;

        }

        public ArrayList findSeqs(string p)
        {
            ArrayList allSeqs = new ArrayList();
            ArrayList seqs = new ArrayList();
            string[] files;
            string seqName = "Default";
            string[] dirs = Directory.GetDirectories(p);
            files = findFiles(p, "*.dseq", false);
            seqs.Add(seqName);
            seqs.Add(files);
            allSeqs.Add(seqs);

            foreach (string dir in dirs)
            {
                ArrayList tmpseqs = new ArrayList();
                files = findFiles(dir, "*.dseq", false);
                string[] dirtokens = dir.Split('\\');
                seqName = dirtokens[dirtokens.Length - 1];
                tmpseqs.Add(seqName);
                tmpseqs.Add(files);
                allSeqs.Add(tmpseqs);

            }
            return allSeqs;

        }

        public string[] findMovesFiles(string p)
        {
            ArrayList moves = new ArrayList();
            string[] files;   
            files = findFiles(p, "*.move", true);
            foreach (string f in files)
            {
                moves.Add(f);
            }

            string[] allMoves = new string[moves.Count];
            moves.CopyTo(allMoves);

            return allMoves;

        }

        // An optionally recursive function that takes a path and file filter "*.foo" and returns an array of all the
        // files matching the filter starting in the specified directory.
        public string[] findFiles(string p, string f, bool recursive)
        {
            string[] files;
            ArrayList recFiles = new ArrayList();

            string[] dirs = Directory.GetDirectories(p);
            foreach (string dir in dirs)
            {

                string[] tmpFiles = findFiles(dir, f, true);
                foreach (string s in tmpFiles)
                {

                    recFiles.Add(s);

                }

            }

            if (dirs.Length > 0 && recursive)
            {
                string[] tmpArray = Directory.GetFiles(p, f);

                files = new String[tmpArray.Length + recFiles.Count];
                tmpArray.CopyTo(files, 0);
                recFiles.CopyTo(files, tmpArray.Length);
                return files;
            }
            else
            {

                string[] tmpArray = Directory.GetFiles(p, f);

                files = new String[tmpArray.Length];
                tmpArray.CopyTo(files, 0);

                return files;

            }
        }

        public ArrayList loadSeqs(string[] files)
        {

            ArrayList tmpArray = new ArrayList();
            progressBar1.Value = 0;
            progressBar1.Step = 100 / files.Length;       
            foreach (string f in files)
            {
                progressBar1.PerformStep();
                DynSequence[] tmpSeqs = loadSequenceFile(f);
                foreach (DynSequence s in tmpSeqs)
                {
                    tmpArray.Add(s);
                }
            }
            progressBar1.Value = 100;
            return tmpArray;
        }

        public DynSequence[] loadSequenceFile(string f)
        {

            ArrayList tmpSeqs = new ArrayList();
            DynSequence curSeq = new DynSequence();

            StreamReader readerSeq = new StreamReader(f);

            int linenum = 0;

            do
            {
                string tmpData = readerSeq.ReadLine();
                linenum++;
                // Remove leading and trailing whitespace and comments
                tmpData = Regex.Replace(tmpData, @"#.*", "");
                tmpData = Regex.Replace(tmpData, @"^\s*", "");
                tmpData = Regex.Replace(tmpData, @"\s*$", "");
                // Sequence start detected, enter new whil loop...
                if (tmpData.StartsWith("DynSequence"))
                {
                    // Use depth to track when the Sequence ends (when it hits 0)... will break with bad formatting, but so will game
                    int depth = 1;
                    curSeq = new DynSequence();
                    curSeq.start = linenum - 1;

                    string[] tokens = Regex.Split(tmpData, @"\s+");
                    curSeq.file = f;
                    curSeq.name = tokens[1];

                    do
                    {
                        tmpData = readerSeq.ReadLine();
                        linenum++;
                        // Remove leading and trailing whitespace and comments
                        tmpData = Regex.Replace(tmpData, @"#.*", "");
                        tmpData = Regex.Replace(tmpData, @"^\s*", "");
                        tmpData = Regex.Replace(tmpData, @"\s*$", "");

                        if (tmpData.StartsWith("RequiresBits"))
                        {
                            tokens = Regex.Split(tmpData, @"\s+");
                            for (int t = 1; t < tokens.Length; t++)
                            {
                                curSeq.bits.Add(tokens[t]);
                            }

                        }
                        else if (tmpData.StartsWith("OptionalBits"))
                        {
                            tokens = Regex.Split(tmpData, @"\s+");
                            for (int t = 1; t < tokens.Length; t++)
                            {
                                curSeq.optionalBits.Add(tokens[t]);
                            }

                        }
                        else if (tmpData.StartsWith("Priority"))
                        {
                            tokens = Regex.Split(tmpData, @"\s+");
                            curSeq.priority = System.Convert.ToInt32(tokens[1]);

                        }
                        else if (tmpData.StartsWith("DynMove"))
                        {
                            tokens = Regex.Split(tmpData, @"\s+");
                            string moveName = tokens[1];
                            curSeq.moves.Add(moveName);
                            depth++;
                        }
                        else if (tmpData.StartsWith("DynAction"))
                        {
                            depth++;
                        }
                        else if (tmpData.StartsWith("If"))
                        {
                            depth++;
                        }
                        else if (tmpData.StartsWith("CallFX"))
                        {
                            depth++;
                        }
                        else if (tmpData.StartsWith("FirstIf"))
                        {
                            depth++;
                        }
                        else if (tmpData.Equals("End"))
                        {
                            depth--;
                        }

                    } while (depth > 0 && readerSeq.Peek() != -1);

                    curSeq.length = linenum - curSeq.start;

                    tmpSeqs.Add(curSeq); 
 
                }

            }
            while (readerSeq.Peek() != -1);

            readerSeq.Close();

            tmpSeqs.Add(curSeq);
            DynSequence[] seqs = new DynSequence[tmpSeqs.Count];
            tmpSeqs.CopyTo(seqs);
            return seqs;

        }

        // This function takes a string array of move names and returns a DynMove array containing those moves
        // should first check project moves then core moves (so that it maintains overrides)
        public DynMove[] findMoves(string[] n)
        {
            ArrayList tmpMoves = new ArrayList();
            foreach (string name in n)
            {
                bool found = false;
                
                foreach (DynMove m in dynMoves)
                {
                    if (m.name.Equals(name))
                    {
                        tmpMoves.Add(m);
                        found = true;
                    }
                }
                // If you found it in the project, no need to scan core (to avoid duplicates)
                // Is there any reason to even do this? Core moves are always available...
                if (!found)
                {
                    foreach (DynMove m in coreDynMoves)
                    {
                        if (m.name.Equals(name))
                        {
                            tmpMoves.Add(m);
                            found = true;
                        }
                    }
                }
            }
            DynMove[] matchs = new DynMove[tmpMoves.Count];
            tmpMoves.CopyTo(matchs);
            return matchs;
        }

        public void moveAnims(DynMove[] m, string p)
        {

            // Copy the danim files to the specified path (project path)
            // Maintain relative path?
            foreach (DynMove move in m)
            {

                foreach (string anim in move.danim)
                {
                    // skip core anims
                    if (!anim.StartsWith("core", System.StringComparison.CurrentCultureIgnoreCase))
                    {
                        string fullpath = pathProjects[comboProject.SelectedIndex] + "\\src\\animation_library\\" + anim;
                        fullpath = fullpath.Replace('/', '\\') + ".danim";
                        string dirpath = "\\src\\animation_library\\";
                        string[] tokens = anim.Split('/');
                        for (int d = 0; d < tokens.Length - 1; d++)
                        {
                            dirpath = dirpath + tokens[d] + "\\";
                        }
                        string target = p + dirpath + tokens[tokens.Length - 1] + ".danim";

                        if (!File.Exists(target))
                        {
                            Directory.CreateDirectory(p + dirpath);
                            File.Copy(fullpath, target);
                        }
                    }
                }
            }
        }

        public bool writeSequence(string f, DynSequence seq)
        {
            string dir = Regex.Replace(f, @"\\[^\\]*$", @"");
            Directory.CreateDirectory(dir);
            if(!File.Exists(f)) File.Create(f);
            FileInfo targetInfo = new FileInfo(f);

            if (targetInfo.IsReadOnly)
            {

                //try to check out...
                System.Windows.Forms.MessageBox.Show("Sequence file animManager.dseq is not checked out, nothing written.");
                return false;

            }
            else
            {
                StreamReader readerSeq = new StreamReader(seq.file);
                StreamWriter writerSeq = File.AppendText(f);

                writerSeq.WriteLine("");
                for (int n = 0; n < seq.start; n++)
                {
                    readerSeq.ReadLine();
                }

                writerSeq.WriteLine("######## Animation Manager Insert ########");

                for (int l = 0; l <= seq.length; l++)
                {

                    writerSeq.WriteLine(readerSeq.ReadLine());

                }
                readerSeq.Close();
                writerSeq.Close();
                return true;
            }
        }

        public bool writeMove(string f, DynMove move)
        {
            string dir = Regex.Replace(f, @"\\[^\\]*$", @"");
            Directory.CreateDirectory(dir);
            if (!File.Exists(f)) File.Create(f);
            FileInfo targetInfo = new FileInfo(f);

            if (targetInfo.IsReadOnly)
            {

                //try to check out...
                System.Windows.Forms.MessageBox.Show("Move file animManager.move is not checked out, nothing written.");
                return false;
               
            }
            else
            {

                StreamReader readerMove = new StreamReader(move.file);
                StreamWriter writerMove = File.AppendText(f);
                
                writerMove.WriteLine("");
                for (int n = 0; n < move.start; n++)
                {
                    readerMove.ReadLine();
                }

                writerMove.WriteLine("######## Animation Manager Insert ########");

                for (int l = 0; l <= move.length; l++)
                {

                    writerMove.WriteLine(readerMove.ReadLine());

                }
                readerMove.Close();
                writerMove.Close();
                return true;
            }

        }

        public ArrayList loadMoves(string[] f)
        {
            ArrayList allMoves = new ArrayList();

            foreach(string file in f)
            {

                DynMove[] tmpMoves = loadMoveFile(file);
                foreach(DynMove m in tmpMoves)
                {

                    allMoves.Add(m);

                }

            }

            return allMoves;

        }

        public DynMove[] loadMoveFile(string f)
        {

            StreamReader readerMove = new StreamReader(f);
            ArrayList moves = new ArrayList();
            int linenum = 0;
            
            do
            {

                string line = readerMove.ReadLine();
                linenum ++;
                // String comments and whitespace
                line = Regex.Replace(line, @"#.*", "");
                line = Regex.Replace(line, @"^\s*", "");
                line = Regex.Replace(line, @"\s*$", "");

                if(line.StartsWith("DynMove"))
                {
                    int depth = 1;
                    DynMove curMove = new DynMove();
                    curMove.start = linenum - 1;

                    string[] tokens = Regex.Split(line, @"\s+");
                    curMove.file = f;
                    curMove.name = tokens[1];

                    do
                    {
                        line = readerMove.ReadLine();
                        linenum++;
                        // String comments and whitespace
                        line = Regex.Replace(line, @"#.*", "");
                        line = Regex.Replace(line, @"^\s*", "");
                        line = Regex.Replace(line, @"\s*$", "");

                        if (line.StartsWith("DynMoveSeq"))
                        {
                            depth++;
                        }
                        else if (line.StartsWith("DynAnimTrack"))
                        {
                            depth++;
                            tokens = Regex.Split(line, @"\s+");
                            curMove.danim.Add(tokens[1]);
                        }
                        else if (line.StartsWith("End"))
                        {
                            depth--;
                        }

                    } while (depth > 0 && readerMove.Peek() != -1);

                    curMove.length = linenum - curMove.start;

                    moves.Add(curMove);

                }


            }while(readerMove.Peek() != -1);

            readerMove.Close();
            DynMove[] allMoves = new DynMove[moves.Count];
            moves.CopyTo(allMoves);
            return allMoves;            

        }

        public int getAnimIndex(string n)
        {
            int index = 0;
            
            for (int a = 0; a < animList.Count; a++ )
            {
                string[] name = (string[])animList[a];
                if (name[0].Equals(n))
                {

                    index = a;

                }

            }

            return index;

        }

        public void updateFields()
        {
            if (listAnims.SelectedIndex >= 0)
            {
                string[] tmpStr = (string[])animList[getAnimIndex((string)listAnims.SelectedItem)];
                textBits.Text = tmpStr[1];
                int[] seqIndex = findSequence(tmpStr[1]);

                if (comboSequencer.SelectedIndex < Sequencers.Count)
                {
                    curSequencer = (ArrayList)Sequencers[comboSequencer.SelectedIndex];
                }
                else
                {
                    curSequencer = (ArrayList)coreSequencers[comboSequencer.SelectedIndex - Sequencers.Count];
                }

                //dynSeqs = loadSeqs((string[])curSequencer[1]);

                DynSequence tmpSeq = new DynSequence();
                if (seqIndex[0] > 0)
                {

                    tmpSeq = (DynSequence)dynSeqs[seqIndex[1]];
                    textSequence.Text = tmpSeq.name;

                }
                else if (seqIndex[0] == -1)
                {

                    textSequence.Text = "No Match Found";

                }
                else
                {

                    tmpSeq = (DynSequence)dynSeqs[seqIndex[1]];
                    textSequence.Text = tmpSeq.name;

                }
                if (seqIndex[0] == -1)
                {

                    textSequence.Text = "No Match Found";
                    textSeqFlie.Text = "NA";

                }
                else
                {
                    textSequence.Text = tmpSeq.name;
                    textSeqFlie.Text = tmpSeq.file;

                }
                listMoves.Items.Clear();
                foreach (string m in tmpSeq.moves)
                {
                    listMoves.Items.Add(m);
                }
            }
            else
            {

                // Do nothing

            }
        }

        // Returns an array [projectID, sequence#] that best matches. Project 0 is assumed to be always be Core, project -1 means no match
        public int[] findSequence(string b)
        {
            string[] bits = Regex.Split(b, @"\s+");
            int bestMatch = 0;
            int[] bestIndex = { 0, 0 };
            DynSequence tmpSequence;

            // check for matches in core
            foreach (DynSequence s in dynSeqs)
            {
                int curMatch = 0;
                int lastMatch;
                //Test for required bits
                foreach (string n in s.bits)
                {
                    lastMatch = curMatch;
                    foreach (string m in bits)
                    {
                        if (m.Equals(n))
                        {
                            curMatch++;
                        }
                    }
                }
                if (curMatch < s.bits.Count)
                {
                    continue;
                }
                foreach (string n in s.optionalBits)
                {
                    foreach (string m in bits)
                    {
                        if (m.Equals(n))
                        {
                            curMatch++;
                        }
                    }
                }
                tmpSequence = (DynSequence)dynSeqs[bestIndex[1]];
                if (curMatch > bestMatch)
                {
                    bestMatch = curMatch;
                    bestIndex[1] = dynSeqs.IndexOf(s);
                }
                else if (curMatch == bestMatch && s.priority > tmpSequence.priority)
                {
                    bestMatch = curMatch;
                    bestIndex[1] = dynSeqs.IndexOf(s);
                }

            }

            if (bestMatch == 0) bestIndex[0] = -1;

            return bestIndex;
        }

        public void copyFiles()
        {

            string[] tmpStr = (string[])animList[getAnimIndex((string)listAnims.SelectedItem)];
            int[] seqIndex = findSequence(tmpStr[1]);
            DynSequence tmpSeq = (DynSequence)dynSeqs[seqIndex[1]];
            if (seqIndex[0] != -1)
            {
                //Determine target path and copy files/data....
                string targetpath = (string)pathProjects[comboTarget.SelectedIndex + 1];
                string[] tmparray = new string[tmpSeq.moves.Count];
                tmpSeq.moves.CopyTo(tmparray);
                DynMove[] tmpMoves = findMoves(tmparray);
                moveAnims(tmpMoves, targetpath);
                foreach (DynMove move in tmpMoves)
                {
                    if (!moveExists(move.name, comboTarget.SelectedIndex + 1, (string)comboSequencer.SelectedItem))
                    {
                        // Determine path
                        string[] pathtokens = move.file.Split('\\');
                        string movetargetfile = "";
                        for (int t = pathtokens.Length - 1; t > 0; t--)
                        {

                            movetargetfile = "\\" + pathtokens[t] + movetargetfile;
                            if(pathtokens[t].Equals("data"))
                            {

                                break;

                            }

                        }

                        writeMove(targetpath + movetargetfile, move);

                    }

                }
                if (!seqExists(tmpSeq.name, comboTarget.SelectedIndex + 1, (string)comboSequencer.SelectedItem))
                {
                    string[] pathtokens = tmpSeq.file.Split('\\');
                    string seqtargetfile = "";
                    for (int t = pathtokens.Length - 1; t > 0; t--)
                    {

                        seqtargetfile = "\\" + pathtokens[t] + seqtargetfile;
                        if (pathtokens[t].Equals("data"))
                        {

                            break;

                        }

                    }

                    writeSequence(targetpath + seqtargetfile, tmpSeq);
                }
                //Deal with Bits...
                string[] bits = new string[tmpSeq.bits.Count];
                tmpSeq.bits.CopyTo(bits);
                foreach (string b in bits)
                {

                    if (!coreBit(b) && !projBit(b, comboTarget.SelectedIndex + 1))
                    {

                        writeModeBit(b, comboTarget.SelectedIndex + 1);

                    }

                }

                System.Windows.Forms.MessageBox.Show("Copy complete.");
            }
            else
            {
                System.Windows.Forms.MessageBox.Show("No matching sequence found to copy.");
            }

        }

        public bool seqExists(string name, int targetID, string sequencer)
        {
            if (sequencer == "Default") sequencer = "";
            string path = pathProjects[targetID] + "\\data\\dyn\\sequence\\" + sequencer;
            if (Directory.Exists(path))
            {

                string[] files = findFiles(path, "*.dseq", false);
                string line;
                foreach (string f in files)
                {

                    StreamReader readerSeq = new StreamReader(f);
                    do
                    {
                        line = readerSeq.ReadLine();
                        if (line != null)
                        {
                            line = Regex.Replace(line, @"#.*", "");
                            line = Regex.Replace(line, @"^\s*", "");
                            line = Regex.Replace(line, @"\s*$", "");

                            string[] tokens = Regex.Split(line, @"\s+");

                            if (tokens[0].Equals("DynSequence") && tokens[1].Equals(name))
                            {

                                readerSeq.Close();
                                return true;

                            }
                        }

                    } while (readerSeq.Peek() != -1);

                    readerSeq.Close();

                }
                return false;

            }
            else
            {
                return false;

            }


        }

        public bool moveExists(string name, int targetID, string sequencer)
        {

            if (sequencer == "Default") sequencer = "";
            string path = pathProjects[targetID] + "\\data\\dyn\\move\\" + sequencer;
            if (Directory.Exists(path))
            {

                string[] files = findFiles(path, "*.move", false);
                string line;
                foreach (string f in files)
                {

                    StreamReader readerSeq = new StreamReader(f);
                    do
                    {
                        line = readerSeq.ReadLine();
                        if (line != null)
                        {
                            line = Regex.Replace(line, @"#.*", "");
                            line = Regex.Replace(line, @"^\s*", "");
                            line = Regex.Replace(line, @"\s*$", "");

                            string[] tokens = Regex.Split(line, @"\s+");

                            if (tokens[0].Equals("DynMove") && tokens[1].Equals(name))
                            {
                                readerSeq.Close();
                                return true;

                            }
                        }

                    } while (readerSeq.Peek() != -1);
                    readerSeq.Close();
                }
                return false;

            }
            else
            {

                return false;

            }

        }

        public bool coreBit(string b)
        {
            string[] filelist = findFiles(pathProjects[0] + pathSeqBits, "*.dbit", false);
            string path = filelist[0];

            bool found = false;

            StreamReader readerBit = new StreamReader(path);

            do
            {
                string line = readerBit.ReadLine();

                line = Regex.Replace(line, @"[#|//].*", "");
                line = Regex.Replace(line, @"^\s*", "");
                line = Regex.Replace(line, @"\s*$", "");

                if (line.StartsWith("ModeBit"))
                {

                    string[] tokens = Regex.Split(line, @"\s+");
                    if (tokens[1].Equals(b))
                    {
                        found = true;
                        break;
                    }
                }

            } while (readerBit.Peek() != -1);

            readerBit.Close();

            return found;

        }

        public bool projBit(string b, int projID)
        {

            string[] filelist = findFiles(pathProjects[projID] + pathSeqBits, "*.dbit", false);
            string path = filelist[0];

            bool found = false;

            StreamReader readerBit = new StreamReader(path);

            do
            {
                string line = readerBit.ReadLine();

                line = Regex.Replace(line, @"[#|//].*", "");
                line = Regex.Replace(line, @"^\s*", "");
                line = Regex.Replace(line, @"\s*$", "");

                if (line.StartsWith("ModeBit"))
                {

                    string[] tokens = Regex.Split(line, @"\s+");
                    if (tokens[1].Equals(b))
                    {
                        found = true;
                        break;
                    }
                }

            } while (readerBit.Peek() != -1);

            readerBit.Close();

            return found;

        }

        public bool checkedOut(string f)
        {

            if (!File.Exists(f))
            {

                return true;

            }
            
            FileInfo targetInfo = new FileInfo(f);

            if (targetInfo.IsReadOnly)
            {

                return false;

            }
            else
            {
                return true;
            }

        }

        public bool writeModeBit(string b, int targetID)
        {
            string[] filelist = findFiles(pathProjects[targetID] + pathSeqBits, "*.dbit", false);
            string target = filelist[0];

            FileInfo bitInfo = new FileInfo(target);

            if (bitInfo.IsReadOnly)
            {

                //try to check out...
                System.Windows.Forms.MessageBox.Show("Bit files is not checked out. Bits not written: " + b);
                return false;

            }
            else
            {

                StreamWriter writerBit = File.AppendText(target);
                writerBit.WriteLine("");

                writerBit.WriteLine("######## Animation Manager Insert ########");

                writerBit.WriteLine("ModeBit\t" + b);

                writerBit.Close();
                return true;
            }

        }

        public void writeDetailBit()
        {



        }

        
    }
}