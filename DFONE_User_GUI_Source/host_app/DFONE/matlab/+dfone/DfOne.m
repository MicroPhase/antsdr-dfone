classdef DfOne < handle
    %DFONE MATLAB wrapper around the DFONE C++ SDK MEX gateway.

    properties
        DeviceIP = '192.168.7.2'
        CommandPort = 49208
        DataPort = 49209
    end

    properties (SetAccess = private)
        Handle = uint64(0)
    end

    methods
        function obj = DfOne(varargin)
            parser = inputParser;
            parser.addParameter('DeviceIP', obj.DeviceIP);
            parser.addParameter('CommandPort', obj.CommandPort);
            parser.addParameter('DataPort', obj.DataPort);
            parser.addParameter('AutoConnect', true);
            parser.parse(varargin{:});

            obj.DeviceIP = char(parser.Results.DeviceIP);
            obj.CommandPort = double(parser.Results.CommandPort);
            obj.DataPort = double(parser.Results.DataPort);

            if parser.Results.AutoConnect
                obj.open();
            end
        end

        function delete(obj)
            obj.close();
        end

        function open(obj)
            obj.close();
            obj.Handle = dfone.dfone_mex( ...
                'open', obj.DeviceIP, uint16(obj.CommandPort), uint16(obj.DataPort));
        end

        function close(obj)
            if obj.Handle ~= 0
                dfone.dfone_mex('close', obj.Handle);
                obj.Handle = uint64(0);
            end
        end

        function tf = isOpen(obj)
            tf = obj.Handle ~= 0 && dfone.dfone_mex('isOpen', obj.Handle);
        end

        function configure(obj, sampleRateHz, rxLoHz, rxGainDb, varargin)
            parser = inputParser;
            parser.addParameter('ReferenceClock', 0);
            parser.parse(varargin{:});

            obj.requireOpen();
            dfone.dfone_mex('configure', ...
                obj.Handle, ...
                uint32(sampleRateHz), ...
                uint64(rxLoHz), ...
                uint32(rxGainDb), ...
                uint32(parser.Results.ReferenceClock));
        end

        function setSampleRate(obj, sampleRateHz)
            obj.requireOpen();
            dfone.dfone_mex('setSampleRate', obj.Handle, uint32(sampleRateHz));
        end

        function setFrequency(obj, rxLoHz)
            obj.requireOpen();
            dfone.dfone_mex('setFrequency', obj.Handle, uint64(rxLoHz));
        end

        function setGain(obj, rxGainDb)
            obj.requireOpen();
            dfone.dfone_mex('setGain', obj.Handle, uint32(rxGainDb));
        end

        function setReferenceClock(obj, source)
            obj.requireOpen();
            dfone.dfone_mex('setReferenceClock', obj.Handle, uint32(source));
        end

        function capture = capture(obj, frames)
            obj.requireOpen();
            capture = dfone.dfone_mex('capture', obj.Handle, uint64(frames), false);
        end

        function capture = captureUncorrected(obj, frames)
            obj.requireOpen();
            capture = dfone.dfone_mex('capture', obj.Handle, uint64(frames), true);
        end
    end

    methods (Access = private)
        function requireOpen(obj)
            if obj.Handle == 0 || ~dfone.dfone_mex('isOpen', obj.Handle)
                error('dfone:DfOne:notOpen', 'DFONE device is not open.');
            end
        end
    end
end
