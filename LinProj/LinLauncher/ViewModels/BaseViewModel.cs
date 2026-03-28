// BaseViewModel.cs: 實作 INotifyPropertyChanged 的基底類別，用於資料繫結通知。
using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace LinLauncher.ViewModels
{
    public class BaseViewModel : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler? PropertyChanged;
        protected void OnPropertyChanged([CallerMemberName] string? name = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    }
}
