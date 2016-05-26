#pragma once

#include <stdafx.h>

namespace cursespp {
    class IInput {
        public:
            virtual ~IInput() { }
            virtual void WriteChar(int64 ch) = 0;
    };
}
