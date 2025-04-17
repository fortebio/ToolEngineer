#include "define.h"
#include "feature.h"

String deseaseConclusion(char result)
{
    if (result == 'P')
        return "Positive";
    else if (result == 'N')
        return "Negative";
    else if (result == 'S')
        return "Slightly Positive";
    return "Unknown";
}
