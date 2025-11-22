namespace MessagePacker
{
    partial class Form1
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
            this.components = new System.ComponentModel.Container();
            this.listBox1 = new System.Windows.Forms.ListBox();
            this.button1 = new System.Windows.Forms.Button();
            this.PrevFolder = new System.Windows.Forms.TextBox();
            this.label1 = new System.Windows.Forms.Label();
            this.label2 = new System.Windows.Forms.Label();
            this.NewFolder = new System.Windows.Forms.TextBox();
            this.listBox2 = new System.Windows.Forms.ListBox();
            this.listbox2Menu = new System.Windows.Forms.ContextMenuStrip(this.components);
            this.modifyToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.deleteToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
            this.label3 = new System.Windows.Forms.Label();
            this.listbox3Menu = new System.Windows.Forms.ContextMenuStrip(this.components);
            this.modifyToolStripMenuItem1 = new System.Windows.Forms.ToolStripMenuItem();
            this.deleteToolStripMenuItem1 = new System.Windows.Forms.ToolStripMenuItem();
            this.Dups_checkBox = new System.Windows.Forms.CheckBox();
            this.CoreFolder = new System.Windows.Forms.TextBox();
            this.ProjectFolder = new System.Windows.Forms.TextBox();
            this.label4 = new System.Windows.Forms.Label();
            this.label5 = new System.Windows.Forms.Label();
            this.dummytrans_checkbox = new System.Windows.Forms.CheckBox();
            this.listbox2Menu.SuspendLayout();
            this.listbox3Menu.SuspendLayout();
            this.SuspendLayout();
            // 
            // listBox1
            // 
            this.listBox1.Font = new System.Drawing.Font("Arial", 9.75F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
            this.listBox1.FormattingEnabled = true;
            this.listBox1.ItemHeight = 16;
            this.listBox1.Location = new System.Drawing.Point(12, 293);
            this.listBox1.Name = "listBox1";
            this.listBox1.Size = new System.Drawing.Size(868, 260);
            this.listBox1.TabIndex = 0;
            // 
            // button1
            // 
            this.button1.DialogResult = System.Windows.Forms.DialogResult.OK;
            this.button1.Location = new System.Drawing.Point(12, 264);
            this.button1.Name = "button1";
            this.button1.Size = new System.Drawing.Size(75, 23);
            this.button1.TabIndex = 3;
            this.button1.Text = "Process";
            this.button1.UseVisualStyleBackColor = true;
            // 
            // PrevFolder
            // 
            this.PrevFolder.Location = new System.Drawing.Point(144, 12);
            this.PrevFolder.Name = "PrevFolder";
            this.PrevFolder.ReadOnly = true;
            this.PrevFolder.Size = new System.Drawing.Size(543, 20);
            this.PrevFolder.TabIndex = 1;
            this.PrevFolder.Click += new System.EventHandler(this.ClickMsgFolder);
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(12, 15);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(126, 13);
            this.label1.TabIndex = 3;
            this.label1.Text = "Previous Message Folder";
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(31, 45);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(107, 13);
            this.label2.TabIndex = 4;
            this.label2.Text = "New Message Folder";
            // 
            // NewFolder
            // 
            this.NewFolder.Location = new System.Drawing.Point(144, 42);
            this.NewFolder.Name = "NewFolder";
            this.NewFolder.ReadOnly = true;
            this.NewFolder.Size = new System.Drawing.Size(543, 20);
            this.NewFolder.TabIndex = 2;
            this.NewFolder.Click += new System.EventHandler(this.ClickMsgFolder);
            // 
            // listBox2
            // 
            this.listBox2.ContextMenuStrip = this.listbox2Menu;
            this.listBox2.FormattingEnabled = true;
            this.listBox2.Location = new System.Drawing.Point(144, 130);
            this.listBox2.Name = "listBox2";
            this.listBox2.ScrollAlwaysVisible = true;
            this.listBox2.Size = new System.Drawing.Size(543, 56);
            this.listBox2.TabIndex = 5;
            this.listBox2.SelectedIndexChanged += new System.EventHandler(this.listBox2_SelectedIndexChanged);
            this.listBox2.DoubleClick += new System.EventHandler(this.AddFolderEntry);
            // 
            // listbox2Menu
            // 
            this.listbox2Menu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.modifyToolStripMenuItem,
            this.deleteToolStripMenuItem});
            this.listbox2Menu.Name = "contextMenuStrip1";
            this.listbox2Menu.Size = new System.Drawing.Size(127, 48);
            this.listbox2Menu.Tag = "";
            // 
            // modifyToolStripMenuItem
            // 
            this.modifyToolStripMenuItem.Name = "modifyToolStripMenuItem";
            this.modifyToolStripMenuItem.Size = new System.Drawing.Size(126, 22);
            this.modifyToolStripMenuItem.Text = "Modify";
            this.modifyToolStripMenuItem.Click += new System.EventHandler(this.ModifyFolderEntry);
            // 
            // deleteToolStripMenuItem
            // 
            this.deleteToolStripMenuItem.Name = "deleteToolStripMenuItem";
            this.deleteToolStripMenuItem.Size = new System.Drawing.Size(126, 22);
            this.deleteToolStripMenuItem.Text = "Delete";
            this.deleteToolStripMenuItem.Click += new System.EventHandler(this.DeleteFolderEntry);
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(68, 130);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(70, 13);
            this.label3.TabIndex = 6;
            this.label3.Text = "Client Folders";
            // 
            // listbox3Menu
            // 
            this.listbox3Menu.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.modifyToolStripMenuItem1,
            this.deleteToolStripMenuItem1});
            this.listbox3Menu.Name = "listbox3Menu";
            this.listbox3Menu.Size = new System.Drawing.Size(127, 48);
            // 
            // modifyToolStripMenuItem1
            // 
            this.modifyToolStripMenuItem1.Name = "modifyToolStripMenuItem1";
            this.modifyToolStripMenuItem1.Size = new System.Drawing.Size(126, 22);
            this.modifyToolStripMenuItem1.Text = "Modify";
            this.modifyToolStripMenuItem1.Click += new System.EventHandler(this.ModifyFolderEntry);
            // 
            // deleteToolStripMenuItem1
            // 
            this.deleteToolStripMenuItem1.Name = "deleteToolStripMenuItem1";
            this.deleteToolStripMenuItem1.Size = new System.Drawing.Size(126, 22);
            this.deleteToolStripMenuItem1.Text = "Delete";
            this.deleteToolStripMenuItem1.Click += new System.EventHandler(this.DeleteFolderEntry);
            // 
            // Dups_checkBox
            // 
            this.Dups_checkBox.AutoSize = true;
            this.Dups_checkBox.Location = new System.Drawing.Point(15, 204);
            this.Dups_checkBox.Name = "Dups_checkBox";
            this.Dups_checkBox.Size = new System.Drawing.Size(119, 17);
            this.Dups_checkBox.TabIndex = 6;
            this.Dups_checkBox.Text = "Remove Duplicates";
            this.Dups_checkBox.UseVisualStyleBackColor = true;
            this.Dups_checkBox.CheckedChanged += new System.EventHandler(this.Dups_checkBox_CheckedChanged);
            // 
            // CoreFolder
            // 
            this.CoreFolder.Location = new System.Drawing.Point(144, 69);
            this.CoreFolder.Name = "CoreFolder";
            this.CoreFolder.ReadOnly = true;
            this.CoreFolder.Size = new System.Drawing.Size(543, 20);
            this.CoreFolder.TabIndex = 3;
            this.CoreFolder.Click += new System.EventHandler(this.ClickMsgFolder);
            // 
            // ProjectFolder
            // 
            this.ProjectFolder.Location = new System.Drawing.Point(144, 96);
            this.ProjectFolder.Name = "ProjectFolder";
            this.ProjectFolder.ReadOnly = true;
            this.ProjectFolder.Size = new System.Drawing.Size(543, 20);
            this.ProjectFolder.TabIndex = 4;
            this.ProjectFolder.Click += new System.EventHandler(this.ClickMsgFolder);
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(77, 72);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(61, 13);
            this.label4.TabIndex = 12;
            this.label4.Text = "Core Folder";
            // 
            // label5
            // 
            this.label5.AutoSize = true;
            this.label5.Location = new System.Drawing.Point(66, 99);
            this.label5.Name = "label5";
            this.label5.Size = new System.Drawing.Size(72, 13);
            this.label5.TabIndex = 13;
            this.label5.Text = "Project Folder";
            // 
            // dummytrans_checkbox
            // 
            this.dummytrans_checkbox.AutoSize = true;
            this.dummytrans_checkbox.Location = new System.Drawing.Point(15, 228);
            this.dummytrans_checkbox.Name = "dummytrans_checkbox";
            this.dummytrans_checkbox.Size = new System.Drawing.Size(165, 17);
            this.dummytrans_checkbox.TabIndex = 14;
            this.dummytrans_checkbox.Text = "Create dummy translation files";
            this.dummytrans_checkbox.UseVisualStyleBackColor = true;
            this.dummytrans_checkbox.CheckedChanged += new System.EventHandler(this.dummytrans_checkbox_CheckedChanged);
            // 
            // Form1
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(892, 562);
            this.Controls.Add(this.dummytrans_checkbox);
            this.Controls.Add(this.label5);
            this.Controls.Add(this.label4);
            this.Controls.Add(this.ProjectFolder);
            this.Controls.Add(this.CoreFolder);
            this.Controls.Add(this.Dups_checkBox);
            this.Controls.Add(this.NewFolder);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.listBox2);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.PrevFolder);
            this.Controls.Add(this.listBox1);
            this.Controls.Add(this.button1);
            this.MaximizeBox = false;
            this.MaximumSize = new System.Drawing.Size(900, 600);
            this.MinimizeBox = false;
            this.MinimumSize = new System.Drawing.Size(900, 600);
            this.Name = "Form1";
            this.SizeGripStyle = System.Windows.Forms.SizeGripStyle.Hide;
            this.Text = "MessagePacker messages";
            this.listbox2Menu.ResumeLayout(false);
            this.listbox3Menu.ResumeLayout(false);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        public System.Windows.Forms.ListBox listBox1;
        private System.Windows.Forms.Button button1;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Label label2;
        public System.Windows.Forms.TextBox PrevFolder;
        public System.Windows.Forms.TextBox NewFolder;
        private System.Windows.Forms.Label label3;
        public System.Windows.Forms.ListBox listBox2;
        private System.Windows.Forms.ToolStripMenuItem modifyToolStripMenuItem;
        private System.Windows.Forms.ToolStripMenuItem deleteToolStripMenuItem;
        public System.Windows.Forms.ContextMenuStrip listbox2Menu;
        private System.Windows.Forms.ContextMenuStrip listbox3Menu;
        private System.Windows.Forms.ToolStripMenuItem modifyToolStripMenuItem1;
        private System.Windows.Forms.ToolStripMenuItem deleteToolStripMenuItem1;
        public System.Windows.Forms.CheckBox Dups_checkBox;
        public System.Windows.Forms.TextBox CoreFolder;
        public System.Windows.Forms.TextBox ProjectFolder;
        private System.Windows.Forms.Label label4;
        private System.Windows.Forms.Label label5;
        public System.Windows.Forms.CheckBox dummytrans_checkbox;


    }
}