function build_dfone_mex()
%BUILD_DFONE_MEX Build the DFONE MATLAB MEX gateway in +dfone.

thisFile = mfilename('fullpath');
matlabDir = fileparts(thisFile);
dfoneRoot = fileparts(matlabDir);
packageDir = fullfile(matlabDir, '+dfone');

if ~exist(packageDir, 'dir')
    mkdir(packageDir);
end

sources = {
    fullfile(matlabDir, 'dfone_mex.cpp')
    fullfile(dfoneRoot, 'src', 'public', 'session.cpp')
    fullfile(dfoneRoot, 'src', 'transport', 'control_client.cpp')
    fullfile(dfoneRoot, 'src', 'transport', 'stream_client.cpp')
};

includes = {
    ['-I' fullfile(dfoneRoot, 'public', 'include')]
    ['-I' fullfile(dfoneRoot, 'src', 'common', 'include')]
};

args = [{'-R2018a', '-outdir', packageDir, '-output', 'dfone_mex'}, ...
        includes(:).', sources(:).'];

if ispc
    args = [args, {'COMPFLAGS=$COMPFLAGS /std:c++17', 'ws2_32.lib'}];
elseif ismac
    args = [args, {'CXXFLAGS=$CXXFLAGS -std=c++17 -pthread', ...
                   'LDFLAGS=$LDFLAGS -pthread'}];
else
    args = [args, {'CXXFLAGS=$CXXFLAGS -std=c++17 -pthread', ...
                   'LDFLAGS=$LDFLAGS -pthread'}];
end

mex(args{:});

fprintf('Built %s\n', fullfile(packageDir, ['dfone_mex.' mexext]));
end
