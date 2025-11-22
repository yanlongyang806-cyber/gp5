namespace LogParser
{
    partial class LogParserSetup
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
            this.textBox1 = new System.Windows.Forms.TextBox();
            this.label1 = new System.Windows.Forms.Label();
            this.OK_button = new System.Windows.Forms.Button();
            this.StartDate = new System.Windows.Forms.DateTimePicker();
            this.EndDate = new System.Windows.Forms.DateTimePicker();
            this.label2 = new System.Windows.Forms.Label();
            this.label3 = new System.Windows.Forms.Label();
            this.FilterStartTime = new System.Windows.Forms.CheckBox();
            this.FilterEndTime = new System.Windows.Forms.CheckBox();
            this.label4 = new System.Windows.Forms.Label();
            this.FilterStrings = new System.Windows.Forms.TextBox();
            this.DisplayMatches = new System.Windows.Forms.CheckBox();
            this.SuspendLayout();
            // 
            // textBox1
            // 
            this.textBox1.Location = new System.Drawing.Point(12, 57);
            this.textBox1.Name = "textBox1";
            this.textBox1.ReadOnly = true;
            this.textBox1.Size = new System.Drawing.Size(575, 20);
            this.textBox1.TabIndex = 1;
            this.textBox1.Click += new System.EventHandler(this.ClickChooseLog);
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(13, 38);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(81, 13);
            this.label1.TabIndex = 2;
            this.label1.Text = "Source Log File";
            // 
            // OK_button
            // 
            this.OK_button.DialogResult = System.Windows.Forms.DialogResult.OK;
            this.OK_button.Location = new System.Drawing.Point(515, 400);
            this.OK_button.Name = "OK_button";
            this.OK_button.Size = new System.Drawing.Size(75, 23);
            this.OK_button.TabIndex = 3;
            this.OK_button.Text = "OK";
            this.OK_button.UseVisualStyleBackColor = true;
            // 
            // StartDate
            // 
            this.StartDate.CustomFormat = "dddd MM/dd/yyyy  hh:mm:ss tt";
            this.StartDate.Format = System.Windows.Forms.DateTimePickerFormat.Custom;
            this.StartDate.Location = new System.Drawing.Point(12, 101);
            this.StartDate.Name = "StartDate";
            this.StartDate.RightToLeft = System.Windows.Forms.RightToLeft.No;
            this.StartDate.Size = new System.Drawing.Size(237, 20);
            this.StartDate.TabIndex = 4;
            this.StartDate.Value = new System.DateTime(2000, 1, 1, 0, 0, 0, 0);
            // 
            // EndDate
            // 
            this.EndDate.CustomFormat = "dddd MM/dd/yyyy  hh:mm:ss tt";
            this.EndDate.Format = System.Windows.Forms.DateTimePickerFormat.Custom;
            this.EndDate.Location = new System.Drawing.Point(12, 149);
            this.EndDate.Name = "EndDate";
            this.EndDate.Size = new System.Drawing.Size(237, 20);
            this.EndDate.TabIndex = 6;
            this.EndDate.Value = new System.DateTime(2999, 1, 1, 0, 0, 0, 0);
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(13, 85);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(83, 13);
            this.label2.TabIndex = 8;
            this.label2.Text = "Start Date/Time";
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(13, 133);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(80, 13);
            this.label3.TabIndex = 9;
            this.label3.Text = "End Date/Time";
            // 
            // FilterStartTime
            // 
            this.FilterStartTime.AutoSize = true;
            this.FilterStartTime.Location = new System.Drawing.Point(273, 101);
            this.FilterStartTime.Name = "FilterStartTime";
            this.FilterStartTime.Size = new System.Drawing.Size(99, 17);
            this.FilterStartTime.TabIndex = 10;
            this.FilterStartTime.Text = "Filter Start Time";
            this.FilterStartTime.UseVisualStyleBackColor = true;
            // 
            // FilterEndTime
            // 
            this.FilterEndTime.AutoSize = true;
            this.FilterEndTime.Location = new System.Drawing.Point(273, 149);
            this.FilterEndTime.Name = "FilterEndTime";
            this.FilterEndTime.Size = new System.Drawing.Size(96, 17);
            this.FilterEndTime.TabIndex = 11;
            this.FilterEndTime.Text = "Filter End Time";
            this.FilterEndTime.UseVisualStyleBackColor = true;
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(16, 182);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(188, 13);
            this.label4.TabIndex = 13;
            this.label4.Text = "Filter Strings   (log line must contain all)";
            // 
            // FilterStrings
            // 
            this.FilterStrings.Location = new System.Drawing.Point(13, 198);
            this.FilterStrings.Multiline = true;
            this.FilterStrings.Name = "FilterStrings";
            this.FilterStrings.ScrollBars = System.Windows.Forms.ScrollBars.Both;
            this.FilterStrings.Size = new System.Drawing.Size(573, 156);
            this.FilterStrings.TabIndex = 14;
            this.FilterStrings.WordWrap = false;
            // 
            // DisplayMatches
            // 
            this.DisplayMatches.AutoSize = true;
            this.DisplayMatches.Location = new System.Drawing.Point(12, 370);
            this.DisplayMatches.Name = "DisplayMatches";
            this.DisplayMatches.Size = new System.Drawing.Size(156, 17);
            this.DisplayMatches.TabIndex = 15;
            this.DisplayMatches.Text = "Display Matching Log Lines";
            this.DisplayMatches.UseVisualStyleBackColor = true;
            // 
            // LogParserSetup
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(598, 435);
            this.Controls.Add(this.DisplayMatches);
            this.Controls.Add(this.FilterStrings);
            this.Controls.Add(this.label4);
            this.Controls.Add(this.FilterEndTime);
            this.Controls.Add(this.FilterStartTime);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.label2);
            this.Controls.Add(this.EndDate);
            this.Controls.Add(this.StartDate);
            this.Controls.Add(this.OK_button);
            this.Controls.Add(this.label1);
            this.Controls.Add(this.textBox1);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.Name = "LogParserSetup";
            this.Text = "Log Parser Setup";
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        public System.Windows.Forms.TextBox textBox1;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Button OK_button;
        public System.Windows.Forms.DateTimePicker StartDate;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.Label label3;
        public System.Windows.Forms.DateTimePicker EndDate;
        private System.Windows.Forms.Label label4;
        public System.Windows.Forms.CheckBox FilterStartTime;
        public System.Windows.Forms.CheckBox FilterEndTime;
        public System.Windows.Forms.TextBox FilterStrings;
        public System.Windows.Forms.CheckBox DisplayMatches;
    }
}