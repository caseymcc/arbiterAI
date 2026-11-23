#include "widgets.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace arbiterAI
{
namespace dashboard
{

gweni::controls::Label *addHeading(gweni::controls::Base *parent, const std::string &text)
{
    gweni::controls::Label *heading=parent->newChild<gweni::controls::Label>();
    heading->setText(text);
    heading->setTextColor(palette::muted);
    heading->setDock(gweni::DockPosition::Top);
    heading->setMargin(gweni::Margin(0, 2, 0, 6));
    return heading;
}

gweni::controls::Base *addCard(gweni::controls::Base *parent, const std::string &title, gweni::DockPosition dock,
    int width, int height)
{
    gweni::controls::Base *card=parent->newChild<gweni::controls::Base>();
    card->setDock(dock);
    if(width>0)
        fixWidth(card, width);
    if(height>0)
        card->setHeight(height);
    card->setMargin(gweni::Margin(0, 0, 12, 12));
    addHeading(card, title);
    return card;
}

gweni::Color stateColor(const std::string &state)
{
    if(state=="Loaded")
        return palette::good;
    if(state=="Ready")
        return palette::info;
    if(state=="Downloading")
        return palette::warn;
    return palette::muted;
}

void fixWidth(gweni::controls::Base *control, int width)
{
    control->setSizeFlags({gweni::SizeFlag::Fixed, gweni::SizeFlag::Expand});
    control->setWidth(width);
}

gweni::controls::Label *addStatRow(gweni::controls::Base *parent, const std::string &name)
{
    gweni::controls::Base *row=parent->newChild<gweni::controls::Base>();
    row->setDock(gweni::DockPosition::Top);
    row->setHeight(20);

    gweni::controls::Label *label=row->newChild<gweni::controls::Label>();
    label->setText(name);
    label->setDock(gweni::DockPosition::Left);

    gweni::controls::Label *value=row->newChild<gweni::controls::Label>();
    value->setText("-");
    value->setDock(gweni::DockPosition::Center);
    value->setTextAlignment(gweni::Alignment::Right | gweni::Alignment::CenterV);
    return value;
}

gweni::controls::DataTable *addTable(gweni::controls::Base *parent, int height,
    const std::vector<std::pair<std::string, int>> &columns)
{
    gweni::controls::DataTable *table=parent->newChild<gweni::controls::DataTable>();
    table->setDock(gweni::DockPosition::Top);
    table->setHeight(height);
    table->setColumnCount(static_cast<int>(columns.size()));
    for(size_t i=0; i<columns.size(); ++i)
    {
        table->setColumnCaption(static_cast<int>(i), columns[i].first);
        table->setColumnWidth(static_cast<int>(i), columns[i].second);
    }
    table->setStriped(true);
    return table;
}

void setRows(gweni::controls::DataTable *table, const std::vector<std::vector<std::string>> &rows,
    const std::string &empty)
{
    table->clear();
    if(rows.empty())
    {
        // in the widest column: a cell clips its text, and there is no colspan
        int widest=0;
        for(int i=1; i<table->getColumnCount(); ++i)
        {
            if(table->getColumnWidth(i)>table->getColumnWidth(widest))
                widest=i;
        }
        gweni::controls::DataTableRow *row=table->addRow();
        row->setCellText(widest, empty);
        row->getCell(widest)->setTextColor(palette::muted);
        return;
    }
    for(const std::vector<std::string> &cells:rows)
    {
        gweni::controls::DataTableRow *row=table->addRow();
        for(size_t i=0; i<cells.size(); ++i)
            row->setCellText(static_cast<int>(i), cells[i]);
    }
}

void showDownloads(gweni::controls::Base *container, const nlohmann::json &downloads)
{
    gweni::controls::Base::List previous=container->getChildren();
    for(gweni::controls::Base *child:previous)
        child->delayedDelete();
    container->setHeight(downloads.is_array()?static_cast<int>(downloads.size())*24:0);

    if(!downloads.is_array())
        return;
    for(const nlohmann::json &download:downloads)
    {
        gweni::controls::Base *row=container->newChild<gweni::controls::Base>();
        row->setDock(gweni::DockPosition::Top);
        row->setHeight(24);

        gweni::controls::Label *name=row->newChild<gweni::controls::Label>();
        name->setText(text(download, "model")+" "+text(download, "variant")+"  Downloading");
        name->setDock(gweni::DockPosition::Left);
        fixWidth(name, 320);

        const double percent=number(download, "percent_complete");
        gweni::controls::ProgressBar *bar=row->newChild<gweni::controls::ProgressBar>();
        bar->setDock(gweni::DockPosition::Left);
        fixWidth(bar, 240);
        bar->setMargin(gweni::Margin(0, 6, 8, 6));
        bar->setAutoLabel(false);
        bar->setProgress(static_cast<float>(std::clamp(percent/100.0, 0.0, 1.0)));

        std::string details=formatFixed(percent, 1)+"%";
        if(number(download, "total_bytes")>0)
            details+="   "+formatBytes(number(download, "bytes_downloaded"))+" / "+formatBytes(number(download, "total_bytes"));
        if(number(download, "speed_mbps")>0)
            details+="   "+formatFixed(number(download, "speed_mbps"), 1)+" MB/s";
        if(number(download, "eta_seconds")>0)
            details+="   ~"+formatFixed(number(download, "eta_seconds"), 0)+"s left";
        gweni::controls::Label *info=row->newChild<gweni::controls::Label>();
        info->setText(details);
        info->setTextColor(palette::muted);
        info->setDock(gweni::DockPosition::Center);
    }
}

void notify(gweni::controls::Base *control, gweni::controls::ToastKind kind, const std::string &text)
{
    if(gweni::controls::Canvas *canvas=control->getCanvas())
        canvas->notify(kind, text);
}

std::string formatFixed(double value, int decimals)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

std::string formatRate(double tokensPerSecond)
{
    return formatFixed(tokensPerSecond, 1)+" t/s";
}

std::string formatMb(double mb)
{
    if(mb>=1024.0)
        return formatFixed(mb/1024.0, 1)+" GB";
    return formatFixed(mb, 0)+" MB";
}

std::string formatBytes(double bytes)
{
    if(bytes>=1073741824.0)
        return formatFixed(bytes/1073741824.0, 1)+" GB";
    if(bytes>=1048576.0)
        return formatFixed(bytes/1048576.0, 1)+" MB";
    return formatFixed(bytes, 0)+" B";
}

std::string formatThousands(long long value)
{
    std::string digits=std::to_string(value<0?-value:value);
    std::string out;

    for(size_t i=0; i<digits.size(); ++i)
    {
        if(i>0 && (digits.size()-i)%3==0)
            out+=',';
        out+=digits[i];
    }
    return value<0?"-"+out:out;
}

std::string urlEncode(const std::string &text)
{
    static const char *hex="0123456789ABCDEF";
    std::string out;

    for(unsigned char c:text)
    {
        if(std::isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~' || c=='!' || c=='*' || c=='\'' || c=='(' || c==')')
        {
            out+=static_cast<char>(c);
        }
        else
        {
            out+='%';
            out+=hex[c>>4];
            out+=hex[c&15];
        }
    }
    return out;
}

bool flag(const nlohmann::json &json, const char *key)
{
    if(!json.is_object())
        return false;
    auto it=json.find(key);
    return it!=json.end() && it->is_boolean() && it->get<bool>();
}

double number(const nlohmann::json &json, const char *key)
{
    if(!json.is_object())
        return 0.0;
    auto value=json.find(key);
    if(value==json.end() || !value->is_number())
        return 0.0;
    return value->get<double>();
}

std::string text(const nlohmann::json &json, const char *key, const std::string &fallback)
{
    if(!json.is_object())
        return fallback;
    auto value=json.find(key);
    if(value==json.end() || !value->is_string())
        return fallback;
    return value->get<std::string>();
}

}//namespace dashboard
}//namespace arbiterAI
