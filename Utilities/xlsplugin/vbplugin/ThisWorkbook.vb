
Public Class ThisWorkbook
    Private WithEvents writeToText As Office.CommandBarButton
    Private selectedCells As Excel.Range

    Private Sub ThisWorkbook_Startup(ByVal sender As Object, ByVal e As System.EventArgs) Handles Me.Startup
        DefineShortcutMenu()
    End Sub

    Private Sub ThisWorkbook_Shutdown(ByVal sender As Object, ByVal e As System.EventArgs) Handles Me.Shutdown

    End Sub

    Private Sub DefineShortcutMenu()

        Dim menuItem As Office.MsoControlType = Office.MsoControlType.msoControlButton
        writeToText = Application.CommandBars("Cell").Controls.Add(Type:=menuItem, _
            Before:=1, Temporary:=True)

        writeToText.Style = Office.MsoButtonStyle.msoButtonCaption
        writeToText.Caption = "Write to a Text File"
        writeToText.Tag = "0"
    End Sub

    Private Sub Application_SheetBeforeRightClick(ByVal Sh _
         As Object, ByVal Target As Microsoft.Office.Interop.Excel.Range, _
         ByRef Cancel As Boolean) Handles ThisApplication.SheetBeforeRightClick

        selectedCells = Target
    End Sub

    Private Sub writeToText_Click(ByVal Ctrl As Office.CommandBarButton, _
        ByRef CancelDefault As Boolean) Handles writeToText.Click

        Try
            Dim currentDateTime As System.DateTime = _
                System.DateTime.Now
            Dim dateStamp As String = _
                currentDateTime.ToString("dMMMMyyyy_hh.mm.ss")

            Dim fileName As String = System.Environment.GetFolderPath( _
                Environment.SpecialFolder.MyDocuments) & "\\" & _
                dateStamp & ".txt"
            Dim sw As System.IO.StreamWriter = New System.IO.StreamWriter(fileName)

            For Each cell As Excel.Range In selectedCells.Cells
                If cell.Value2 IsNot Nothing Then
                    sw.WriteLine(cell.Value2.ToString())
                End If
            Next
            sw.Close()
        Catch ex As Exception
            System.Windows.Forms.MessageBox.Show(ex.Message)
        End Try

    End Sub
End Class
