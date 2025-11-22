using System.Collections;
using System.IO;
using System.Diagnostics;

namespace Cryptic_Animation_Manager
{
    partial class frmAnimManager
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this.btnAnimList = new System.Windows.Forms.RadioButton();
            this.btnPowerArt = new System.Windows.Forms.RadioButton();
            this.grpMode = new System.Windows.Forms.GroupBox();
            this.comboProject = new System.Windows.Forms.ComboBox();
            this.comboSequencer = new System.Windows.Forms.ComboBox();
            this.listAnims = new System.Windows.Forms.ListBox();
            this.lblBits = new System.Windows.Forms.Label();
            this.textBits = new System.Windows.Forms.Label();
            this.textSequence = new System.Windows.Forms.Label();
            this.lblSequence = new System.Windows.Forms.Label();
            this.lblMoves = new System.Windows.Forms.Label();
            this.grpTarget = new System.Windows.Forms.GroupBox();
            this.btnCopy = new System.Windows.Forms.Button();
            this.comboTarget = new System.Windows.Forms.ComboBox();
            this.listMoves = new System.Windows.Forms.ListView();
            this.textSeqFlie = new System.Windows.Forms.Label();
            this.progressBar1 = new System.Windows.Forms.ProgressBar();
            this.label1 = new System.Windows.Forms.Label();
            this.grpMode.SuspendLayout();
            this.grpTarget.SuspendLayout();
            this.SuspendLayout();
            // 
            // btnAnimList
            // 
            this.btnAnimList.AutoSize = true;
            this.btnAnimList.Checked = true;
            this.btnAnimList.Location = new System.Drawing.Point(6, 19);
            this.btnAnimList.Name = "btnAnimList";
            this.btnAnimList.Size = new System.Drawing.Size(64, 17);
            this.btnAnimList.TabIndex = 0;
            this.btnAnimList.TabStop = true;
            this.btnAnimList.Text = "AnimList";
            this.btnAnimList.UseVisualStyleBackColor = true;
            this.btnAnimList.Click += new System.EventHandler(this.btnAnimList_Click);
            // 
            // btnPowerArt
            // 
            this.btnPowerArt.AutoSize = true;
            this.btnPowerArt.Location = new System.Drawing.Point(88, 19);
            this.btnPowerArt.Name = "btnPowerArt";
            this.btnPowerArt.Size = new System.Drawing.Size(68, 17);
            this.btnPowerArt.TabIndex = 1;
            this.btnPowerArt.Text = "PowerArt";
            this.btnPowerArt.UseVisualStyleBackColor = true;
            this.btnPowerArt.Click += new System.EventHandler(this.btnPowerArt_Click);
            // 
            // grpMode
            // 
            this.grpMode.Controls.Add(this.comboProject);
            this.grpMode.Controls.Add(this.btnPowerArt);
            this.grpMode.Controls.Add(this.btnAnimList);
            this.grpMode.Location = new System.Drawing.Point(12, 12);
            this.grpMode.Name = "grpMode";
            this.grpMode.Size = new System.Drawing.Size(322, 47);
            this.grpMode.TabIndex = 2;
            this.grpMode.TabStop = false;
            this.grpMode.Text = "Selection Mode";
            // 
            // comboProject
            // 
            this.comboProject.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.comboProject.FormattingEnabled = true;
            this.comboProject.Location = new System.Drawing.Point(172, 18);
            this.comboProject.Name = "comboProject";
            this.comboProject.Size = new System.Drawing.Size(144, 21);
            this.comboProject.TabIndex = 4;
            this.comboProject.SelectedIndexChanged += new System.EventHandler(this.comboProject_SelectedIndexChanged);
            // 
            // comboSequencer
            // 
            this.comboSequencer.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.comboSequencer.FormattingEnabled = true;
            this.comboSequencer.Location = new System.Drawing.Point(599, 30);
            this.comboSequencer.Name = "comboSequencer";
            this.comboSequencer.Size = new System.Drawing.Size(144, 21);
            this.comboSequencer.TabIndex = 5;
            this.comboSequencer.SelectedIndexChanged += new System.EventHandler(this.comboSequencer_SelectedIndexChanged);
            // 
            // listAnims
            // 
            this.listAnims.FormattingEnabled = true;
            this.listAnims.Location = new System.Drawing.Point(12, 76);
            this.listAnims.Name = "listAnims";
            this.listAnims.Size = new System.Drawing.Size(322, 381);
            this.listAnims.Sorted = true;
            this.listAnims.TabIndex = 3;
            this.listAnims.SelectedIndexChanged += new System.EventHandler(this.listAnims_SelectedIndexChanged);
            // 
            // lblBits
            // 
            this.lblBits.AutoSize = true;
            this.lblBits.Font = new System.Drawing.Font("Microsoft Sans Serif", 10F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.lblBits.Location = new System.Drawing.Point(348, 76);
            this.lblBits.Name = "lblBits";
            this.lblBits.Size = new System.Drawing.Size(40, 17);
            this.lblBits.TabIndex = 4;
            this.lblBits.Text = "Bits:";
            // 
            // textBits
            // 
            this.textBits.AutoSize = true;
            this.textBits.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.textBits.Location = new System.Drawing.Point(366, 96);
            this.textBits.Name = "textBits";
            this.textBits.Size = new System.Drawing.Size(0, 13);
            this.textBits.TabIndex = 5;
            // 
            // textSequence
            // 
            this.textSequence.AutoSize = true;
            this.textSequence.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.textSequence.Location = new System.Drawing.Point(366, 163);
            this.textSequence.Name = "textSequence";
            this.textSequence.Size = new System.Drawing.Size(0, 13);
            this.textSequence.TabIndex = 7;
            // 
            // lblSequence
            // 
            this.lblSequence.AutoSize = true;
            this.lblSequence.Font = new System.Drawing.Font("Microsoft Sans Serif", 10F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.lblSequence.Location = new System.Drawing.Point(348, 143);
            this.lblSequence.Name = "lblSequence";
            this.lblSequence.Size = new System.Drawing.Size(85, 17);
            this.lblSequence.TabIndex = 6;
            this.lblSequence.Text = "Sequence:";
            // 
            // lblMoves
            // 
            this.lblMoves.AutoSize = true;
            this.lblMoves.Font = new System.Drawing.Font("Microsoft Sans Serif", 10F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.lblMoves.Location = new System.Drawing.Point(348, 203);
            this.lblMoves.Name = "lblMoves";
            this.lblMoves.Size = new System.Drawing.Size(59, 17);
            this.lblMoves.TabIndex = 9;
            this.lblMoves.Text = "Moves:";
            // 
            // grpTarget
            // 
            this.grpTarget.Controls.Add(this.btnCopy);
            this.grpTarget.Controls.Add(this.comboTarget);
            this.grpTarget.Location = new System.Drawing.Point(12, 469);
            this.grpTarget.Name = "grpTarget";
            this.grpTarget.Size = new System.Drawing.Size(316, 52);
            this.grpTarget.TabIndex = 5;
            this.grpTarget.TabStop = false;
            this.grpTarget.Text = "Target";
            // 
            // btnCopy
            // 
            this.btnCopy.Location = new System.Drawing.Point(171, 13);
            this.btnCopy.Name = "btnCopy";
            this.btnCopy.Size = new System.Drawing.Size(138, 31);
            this.btnCopy.TabIndex = 5;
            this.btnCopy.Text = "Copy";
            this.btnCopy.UseVisualStyleBackColor = true;
            this.btnCopy.Click += new System.EventHandler(this.btnCopy_Click);
            // 
            // comboTarget
            // 
            this.comboTarget.FormattingEnabled = true;
            this.comboTarget.Location = new System.Drawing.Point(6, 19);
            this.comboTarget.Name = "comboTarget";
            this.comboTarget.Size = new System.Drawing.Size(144, 21);
            this.comboTarget.TabIndex = 4;
            // 
            // listMoves
            // 
            this.listMoves.BackColor = System.Drawing.SystemColors.Control;
            this.listMoves.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.listMoves.Location = new System.Drawing.Point(351, 223);
            this.listMoves.Name = "listMoves";
            this.listMoves.Size = new System.Drawing.Size(392, 234);
            this.listMoves.TabIndex = 8;
            this.listMoves.UseCompatibleStateImageBehavior = false;
            this.listMoves.View = System.Windows.Forms.View.List;
            // 
            // textSeqFlie
            // 
            this.textSeqFlie.AutoSize = true;
            this.textSeqFlie.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Bold, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.textSeqFlie.Location = new System.Drawing.Point(366, 182);
            this.textSeqFlie.Name = "textSeqFlie";
            this.textSeqFlie.Size = new System.Drawing.Size(92, 13);
            this.textSeqFlie.TabIndex = 10;
            this.textSeqFlie.Text = "Default_Attack";
            // 
            // progressBar1
            // 
            this.progressBar1.Location = new System.Drawing.Point(12, 528);
            this.progressBar1.Name = "progressBar1";
            this.progressBar1.Size = new System.Drawing.Size(731, 19);
            this.progressBar1.TabIndex = 11;
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(531, 33);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(62, 13);
            this.label1.TabIndex = 6;
            this.label1.Text = "Sequencer:";
            // 
            // frmAnimManager
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(755, 555);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.comboSequencer);
            this.Controls.Add(this.progressBar1);
            this.Controls.Add(this.textSeqFlie);
            this.Controls.Add(this.grpTarget);
            this.Controls.Add(this.lblMoves);
            this.Controls.Add(this.listMoves);
            this.Controls.Add(this.textSequence);
            this.Controls.Add(this.lblSequence);
            this.Controls.Add(this.textBits);
            this.Controls.Add(this.lblBits);
            this.Controls.Add(this.listAnims);
            this.Controls.Add(this.grpMode);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedSingle;
            this.MaximizeBox = false;
            this.Name = "frmAnimManager";
            this.Text = "Cryptic Animation Manager";
            this.grpMode.ResumeLayout(false);
            this.grpMode.PerformLayout();
            this.grpTarget.ResumeLayout(false);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        void btnAnimList_Click(object sender, System.EventArgs e)
        {
            listAnims.Sorted = true;
            loadAnimList(comboProject.SelectedIndex);
        }

        void btnPowerArt_Click(object sender, System.EventArgs e)
        {
            listAnims.Sorted = false;
            loadPowerArts(comboProject.SelectedIndex);
        }

        void btnCopy_Click(object sender, System.EventArgs e)
        {           
            
            // check to make sure everything is checked out...
            bool failed = false;
            string[] tmpStr = (string[])animList[getAnimIndex((string)listAnims.SelectedItem)];
            int[] seqIndex = findSequence(tmpStr[1]);
            DynSequence tmpSeq = (DynSequence)dynSeqs[seqIndex[1]];

            string[] seqpathtokens = tmpSeq.file.Split('\\');
            string seqtargetfile = "";
            for (int t = seqpathtokens.Length  - 1; t > 0; t--)
            {

                seqtargetfile = "\\" + seqpathtokens[t] + seqtargetfile;
                if (seqpathtokens[t].Equals("data"))
                {

                    break;

                }

            }

            string[] tmparray = new string[tmpSeq.moves.Count];
            tmpSeq.moves.CopyTo(tmparray);
            DynMove[] tmpMoves = findMoves(tmparray);
            ArrayList movetarget = new ArrayList();
            foreach (DynMove move in tmpMoves)
            {
                string[] movepathtokens = move.file.Split('\\');
                string movetargetfile = "";
                for (int t = movepathtokens.Length - 1; t > 0; t--)
                {

                    movetargetfile = "\\" + movepathtokens[t] + movetargetfile;
                    if (movepathtokens[t].Equals("data"))
                    {

                        break;

                    }
                }
                movetarget.Add(movetargetfile);
            }
            foreach (string file in movetarget)
            {
                
                // check out files!

                
                
                // See if the file can be opened for writing...

                if (!checkedOut((string)pathProjects[comboTarget.SelectedIndex + 1] + file))
                {
                    Process gimme = Process.Start("c:\\Cryptic\\tools\\bin\\gimme.exe", "-editor NULL " +(string)pathProjects[comboTarget.SelectedIndex + 1] + file);

                    do
                    {

                        // do nothing, wait for gimme

                    } while (!gimme.HasExited);

                    if (gimme.ExitCode != 0)
                    {

                        System.Windows.Forms.MessageBox.Show("Couldn't check out move file.");
                        failed = true;

                    }

                }
                try
                {
                    StreamWriter testWriter = new StreamWriter((string)pathProjects[comboTarget.SelectedIndex + 1] + file, true);
                    testWriter.Close();
                }
                catch
                {
                    failed = true;
                    System.Windows.Forms.MessageBox.Show(file + " is open in another application.");
                }
            }

            
            if (!checkedOut(pathProjects[comboTarget.SelectedIndex + 1] + seqtargetfile))
            {
                Process gimmeSeq = Process.Start("C:\\Cryptic\\tools\\bin\\gimme.exe", "-editor NULL " + (string)pathProjects[comboTarget.SelectedIndex + 1] + seqtargetfile);

                do
                {

                    // do nothing, wait for gimme

                } while (!gimmeSeq.HasExited);

                if (gimmeSeq.ExitCode != 0)
                {

                    System.Windows.Forms.MessageBox.Show("Couldn't check out sequence file.");
                    failed = true;

                }

            }
            try
            {
                StreamWriter testWriter = new StreamWriter(pathProjects[comboTarget.SelectedIndex + 1] + seqtargetfile, true);
                testWriter.Close();
            }
            catch
            {
                failed = true;
                System.Windows.Forms.MessageBox.Show(seqtargetfile + " is open in another application.");
            }

            if (!checkedOut(pathProjects[comboTarget.SelectedIndex + 1] + "\\data\\dyn\\seqbits\\seqbits.dbit"))
            {
                Process gimmeSeqbit = Process.Start("C:\\Cryptic\\tools\\bin\\gimme.exe", "-editor NULL " + (string)pathProjects[comboTarget.SelectedIndex + 1] + "\\data\\dyn\\seqbits\\seqbits.dbit");

                do
                {

                    // do nothing, wait for gimme

                } while (!gimmeSeqbit.HasExited);

                if (gimmeSeqbit.ExitCode != 0)
                {

                    System.Windows.Forms.MessageBox.Show("Couldn't check out seqbit file.");
                    failed = true;

                }
            }
            try
            {
                StreamWriter testWriter = new StreamWriter(pathProjects[comboTarget.SelectedIndex + 1] + "\\data\\dyn\\seqbits\\seqbits.dbit", true);
                testWriter.Close();
            }
            catch
            {
                failed = true;
                System.Windows.Forms.MessageBox.Show("seqbits is open in another application.");
            }
            if (!failed)
            {
                copyFiles();
            }
            else
            {
                System.Windows.Forms.MessageBox.Show("Copy Failed.");
            }
        }

        void comboSequencer_SelectedIndexChanged(object sender, System.EventArgs e)
        {

            if (comboSequencer.SelectedIndex < Sequencers.Count)
            {
                curSequencer = (ArrayList)Sequencers[comboSequencer.SelectedIndex];
            }
            else
            {

                curSequencer = (ArrayList)coreSequencers[comboSequencer.SelectedIndex - Sequencers.Count];

            }

            dynSeqs = loadSeqs((string[])curSequencer[1]);

            updateFields();

        }

        void listAnims_SelectedIndexChanged(object sender, System.EventArgs e)
        {
            updateFields();   
        }

        void comboProject_SelectedIndexChanged(object sender, System.EventArgs e)
        {
            if (btnAnimList.Checked) { loadAnimList(comboProject.SelectedIndex); } else { loadPowerArts(comboProject.SelectedIndex); }
            listAnims.SelectedIndex = 0;
        }

        #endregion

        private System.Windows.Forms.RadioButton btnAnimList;
        private System.Windows.Forms.RadioButton btnPowerArt;
        private System.Windows.Forms.GroupBox grpMode;
        private System.Windows.Forms.ListBox listAnims;
        private System.Windows.Forms.ComboBox comboProject;
        private System.Windows.Forms.Label lblBits;
        private System.Windows.Forms.Label textBits;
        private System.Windows.Forms.Label textSequence;
        private System.Windows.Forms.Label lblSequence;
        private System.Windows.Forms.Label lblMoves;
        private System.Windows.Forms.GroupBox grpTarget;
        private System.Windows.Forms.ComboBox comboTarget;
        private System.Windows.Forms.Button btnCopy;
        private System.Windows.Forms.ListView listMoves;
        private System.Windows.Forms.ComboBox comboSequencer;
        private System.Windows.Forms.Label textSeqFlie;
        private System.Windows.Forms.ProgressBar progressBar1;
        private System.Windows.Forms.Label label1;
    }
}

