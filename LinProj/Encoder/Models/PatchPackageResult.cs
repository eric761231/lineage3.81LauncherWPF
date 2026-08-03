using System.Collections.Generic;

namespace LinEncoder.Models
{
    public sealed class PatchPackageResult
    {
        public bool Success { get; init; }
        public string? ErrorMessage { get; init; }
        public string? UpdateListPath { get; init; }
        public List<PatchFileRow> Files { get; init; } = new();
    }
}
