// WPF 與 WinForms 同時參考時，明確以 WPF 類型為預設，避免 Application / MessageBox / Binding 模稜兩可。
global using Application = System.Windows.Application;
global using MessageBox = System.Windows.MessageBox;
global using Binding = System.Windows.Data.Binding;
