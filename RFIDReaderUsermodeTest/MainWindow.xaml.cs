using System;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows;
using Microsoft.Win32.SafeHandles;

namespace RFIDReaderUsermodeTest
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
        }

        private void RequestBtn_OnClick(object sender, RoutedEventArgs e)
        {
            idValue.Content = RFIDReader.RequestReaderData(0);
        }
    }
}
