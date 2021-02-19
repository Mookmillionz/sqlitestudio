#include "sqlqueryitem.h"
#include "common/utils_sql.h"
#include "sqlquerymodel.h"
#include "iconmanager.h"
#include "uiconfig.h"
#include "sqlqueryview.h"
#include <QDate>
#include <QDebug>
#include <QApplication>
#include <QStyle>

SqlQueryItem::SqlQueryItem(QObject *parent) :
    QObject(parent)
{
    setUncommitted(false);
    setCommittingError(false);
    setRowId(RowId());
    setColumn(nullptr);
}

SqlQueryItem::SqlQueryItem(const SqlQueryItem &item)
    : QObject(item.QObject::parent()), QStandardItem(item)
{
}

QStandardItem *SqlQueryItem::clone() const
{
    return new SqlQueryItem(*this);
}

RowId SqlQueryItem::getRowId() const
{
    return QStandardItem::data(DataRole::ROWID).toHash();
}

void SqlQueryItem::setRowId(const RowId& rowId)
{
    QStandardItem::setData(rowId, DataRole::ROWID);
}

bool SqlQueryItem::isUncommitted() const
{
    return QStandardItem::data(DataRole::UNCOMMITTED).toBool();
}

void SqlQueryItem::setUncommitted(bool uncommitted)
{
    QStandardItem::setData(QVariant(uncommitted), DataRole::UNCOMMITTED);
    if (!uncommitted)
    {
        clearOldValue();
        setCommittingError(false);
    }
}

void SqlQueryItem::rollback()
{
    setValue(getOldValue(), getOldValueLimited(), true);
    setUncommitted(false);
    setDeletedRow(false);
}

bool SqlQueryItem::isCommittingError() const
{
    return QStandardItem::data(DataRole::COMMITTING_ERROR).toBool();
}

void SqlQueryItem::setCommittingError(bool isError)
{
    QStandardItem::setData(QVariant(isError), DataRole::COMMITTING_ERROR);
    if (!isError)
        setCommittingErrorMessage(QString());
}

void SqlQueryItem::setCommittingError(bool isError, const QString& msg)
{
    setCommittingErrorMessage(msg);
    setCommittingError(isError);
}

QString SqlQueryItem::getCommittingErrorMessage() const
{
    return QStandardItem::data(DataRole::COMMITTING_ERROR_MESSAGE).toString();
}

void SqlQueryItem::setCommittingErrorMessage(const QString& value)
{
    QStandardItem::setData(QVariant(value), DataRole::COMMITTING_ERROR_MESSAGE);
}

bool SqlQueryItem::isNewRow() const
{
    return QStandardItem::data(DataRole::NEW_ROW).toBool();
}

void SqlQueryItem::setNewRow(bool isNew)
{
    QStandardItem::setData(QVariant(isNew), DataRole::NEW_ROW);
}

bool SqlQueryItem::isJustInsertedWithOutRowId() const
{
    return QStandardItem::data(DataRole::JUST_INSERTED_WITHOUT_ROWID).toBool();
}

void SqlQueryItem::setJustInsertedWithOutRowId(bool justInsertedWithOutRowId)
{
    QStandardItem::setData(QVariant(justInsertedWithOutRowId), DataRole::JUST_INSERTED_WITHOUT_ROWID);
}

bool SqlQueryItem::isDeletedRow() const
{
    return QStandardItem::data(DataRole::DELETED).toBool();
}

void SqlQueryItem::setDeletedRow(bool isDeleted)
{
    if (isDeleted && !getOldValue().isValid())
        rememberOldValue();

    QStandardItem::setData(QVariant(isDeleted), DataRole::DELETED);
}

QVariant SqlQueryItem::getValue() const
{
    return QStandardItem::data(DataRole::VALUE);
}

void SqlQueryItem::setValue(const QVariant &value, bool limited, bool loadedFromDb)
{
    if (!valueSettingLock.tryLock())
    {
        // Triggered recursively by catching "itemChanged" event,
        // that was caused by the QStandardItem::setData below.
        return;
    }

    QVariant newValue = adjustVariantType(value);
    QVariant origValue = getValue();

    // It's modified when:
    // - original and new value is different (value or NULL status), while it's not loading from DB
    // - this item was already marked as uncommitted
    bool modified = (
                        (
                                newValue != origValue ||
                                origValue.isNull() != newValue.isNull()
                        ) &&
                        !loadedFromDb
                    ) ||
                    isUncommitted();

    if (modified && !getOldValue().isValid())
        rememberOldValue();

    // This is a workaround for an issue in Qt, that uses operator== to compare values in QStandardItem::setData().
    // If the old value is null and the new value is empty, then operator == returns true, which is a lie.
    // We need to trick QStandardItem to force updating the value. We feed it with any non-empty value,
    // then we can set whatever value we want and it should be updated (or not, if it's truly the same value).
    QStandardItem::setData("x", DataRole::VALUE);

    QStandardItem::setData(newValue, DataRole::VALUE);
    setLimitedValue(limited);
    setUncommitted(modified);

    // Value for display (in a cell) will always be limited, for performance reasons
    setValueForDisplay("x"); // the same trick as with the DataRole::VALUE
    setValueForDisplay(newValue);

    if (modified && getModel())
        getModel()->itemValueEdited(this);

    valueSettingLock.unlock();
}

bool SqlQueryItem::isLimitedValue() const
{
    return QStandardItem::data(DataRole::LIMITED_VALUE).toBool();
}

QVariant SqlQueryItem::getOldValue() const
{
    return QStandardItem::data(DataRole::OLD_VALUE);
}

void SqlQueryItem::setOldValue(const QVariant& value)
{
    QStandardItem::setData(value, DataRole::OLD_VALUE);
}

bool SqlQueryItem::getOldValueLimited() const
{
    return QStandardItem::data(DataRole::OLD_VALUE_LIMITED).toBool();
}

void SqlQueryItem::setOldValueLimited(bool value)
{
    QStandardItem::setData(value, DataRole::OLD_VALUE_LIMITED);
}

QVariant SqlQueryItem::getValueForDisplay() const
{
    return QStandardItem::data(DataRole::VALUE_FOR_DISPLAY);
}

void SqlQueryItem::setValueForDisplay(const QVariant &value)
{
    QStandardItem::setData(value, DataRole::VALUE_FOR_DISPLAY);
}

void SqlQueryItem::setLimitedValue(bool limited)
{
    QStandardItem::setData(QVariant(limited), DataRole::LIMITED_VALUE);
}

QVariant SqlQueryItem::adjustVariantType(const QVariant& value)
{
    QVariant newValue;
    bool ok;
    newValue = value.toLongLong(&ok);
    if (ok)
    {
        ok = (value.toString() == newValue.toString());
        if (ok)
            return newValue;
    }

    newValue = value.toDouble(&ok);
    if (ok)
    {
        ok = (value.toString() == newValue.toString());
        if (ok)
            return newValue;
    }

    return value;
}

QString SqlQueryItem::getToolTip() const
{
    static const QString tableTmp = "<table>%1</table>";
    static const QString rowTmp = "<tr><td colspan=2 style=\"white-space: pre\">%1</td><td style=\"align: right\"><b>%2</b></td></tr>";
    static const QString hdrRowTmp = "<tr><td width=16><img src=\"%1\"/></td><th colspan=2 style=\"align: center\">%2 %3</th></tr>";
    static const QString constrRowTmp = "<tr><td width=16><img src=\"%1\"/></td><td style=\"white-space: pre\"><b>%2</b></td><td>%3</td></tr>";
    static const QString emptyRow = "<tr><td colspan=3></td></tr>";
    static const QString topErrorRowTmp = "<tr><td width=16><img src=\"%1\"/></td><td style=\"white-space: pre\"><b>%2</b></td><td>%3</td></tr>";

    if (!index().isValid())
        return QString();

    SqlQueryModelColumn* col = getColumn();
    if (!col)
        return QString(); // happens when simple execution method was performed

    QStringList rows;
    if (isCommittingError())
    {
        rows << topErrorRowTmp.arg(ICONS.STATUS_WARNING.getPath(), tr("Committing error:", "data view tooltip"), getCommittingErrorMessage());
        rows << emptyRow;
    }

    rows << hdrRowTmp.arg(ICONS.COLUMN.getPath(), tr("Column:", "data view tooltip"), col->column);
    rows << rowTmp.arg(tr("Data type:", "data view"), col->dataType.toString());
    if (!col->table.isNull())
    {
        rows << rowTmp.arg(tr("Table:", "data view tooltip"), col->table);

        RowId rowId = getRowId();
        QString rowIdStr;
        if (rowId.size() == 1)
        {
            rowIdStr = rowId.values().first().toString();
        }
        else
        {
            QStringList values;
            QString rowIdValue;
            QHashIterator<QString,QVariant> it(rowId);
            while (it.hasNext())
            {
                it.next();
                rowIdValue = it.value().toString();
                if (rowIdValue.length() > 30)
                    rowIdValue = rowIdValue.left(27) + "...";

                values << it.key() + "=" + rowIdValue;
            }
            rowIdStr = "[" + values.join(", ") + "]";
        }
        rows << rowTmp.arg("ROWID:", rowIdStr);
    }

    if (col->constraints.size() > 0)
    {
        rows << emptyRow;
        rows << hdrRowTmp.arg(ICONS.COLUMN_CONSTRAINT.getPath(), tr("Constraints:", "data view tooltip"), "");
        for (SqlQueryModelColumn::Constraint* constr : col->constraints)
            rows << constrRowTmp.arg(constr->getIcon()->toUrl(), constr->getTypeString(), constr->getDetails());
    }

    return tableTmp.arg(rows.join(""));
}

void SqlQueryItem::rememberOldValue()
{
    setOldValue(getValue());
    setOldValueLimited(isLimitedValue());
}

void SqlQueryItem::clearOldValue()
{
    setOldValue(QVariant());
    setOldValueLimited(false);
}

SqlQueryModelColumn* SqlQueryItem::getColumn() const
{
    return QStandardItem::data(DataRole::COLUMN).value<SqlQueryModelColumn*>();
}

void SqlQueryItem::setColumn(SqlQueryModelColumn* column)
{
    QStandardItem::setData(QVariant::fromValue(column), DataRole::COLUMN);
}

SqlQueryModel *SqlQueryItem::getModel() const
{
    if (!model())
        return nullptr;

    return dynamic_cast<SqlQueryModel*>(model());
}

void SqlQueryItem::setData(const QVariant &value, int role)
{
    switch (role)
    {
        case Qt::EditRole:
        {
            // -1 column is used by Qt for header items (ie. when setHeaderData() is called)
            // and we want this to mean that the value was loaded from db, because it forces
            // the value to be interpreted as not modified.
            setValue(value, false, (column() == -1));
            return;
        }
    }

    QStandardItem::setData(value, role);
}

QVariant SqlQueryItem::data(int role) const
{
    switch (role)
    {
        case Qt::EditRole:
        {
            if (isDeletedRow())
                return QVariant();

            return getValue();
        }
        case Qt::DisplayRole:
        {
            if (isDeletedRow())
                return "";

            QVariant value = getValueForDisplay();
            if (value.isNull())
                return "NULL";

            return value;
        }
        case Qt::ForegroundRole:
        {
            QVariant value = getValue();
            if (value.isNull())
                return QApplication::style()->standardPalette().dark();

            break;
        }
        case Qt::BackgroundRole:
        {
            if (isDeletedRow())
                return QApplication::style()->standardPalette().dark();

            break;
        }
        case Qt::TextAlignmentRole:
        {
            QVariant value = getValue();
            if (value.isNull() || isDeletedRow())
                return Qt::AlignCenter;

            break;
        }
        case Qt::FontRole:
        {
            QFont font = CFG_UI.Fonts.DataView.get();

            QVariant value = getValue();
            if (value.isNull() || isDeletedRow())
                font.setItalic(true);

            return font;
        }
        case Qt::ToolTipRole:
        {
            if (!CFG_UI.General.ShowDataViewTooltips.get() || getModel()->getView()->getSimpleBrowserMode())
                return QVariant();

            return getToolTip();
        }
    }

    return QStandardItem::data(role);
}

QString SqlQueryItem::loadFullData()
{
    SqlQueryModelColumn* col = getColumn();

    // Yes, this function won't be called in case of trying to edit the cell - it's handled in the Editor.
    // However this function can be called from the FormView, to display full contents of the read-only property.
    // I'll keep it for some time just in case. To be removed in future.
//    if (col->editionForbiddenReason.size() > 0)
//    {
//        qWarning() << "Tried to load full cell which is not editable. This should be already handled in Editor class when invoking edition action.";
//        return tr("This cell is not editable, because: %1").arg(SqlQueryModelColumn::resolveMessage(col->editionForbiddenReason.values().first()));
//    }

    // This should not happen anymore (since WITHOUT ROWID tables should be handled properly now,
    // but we will keep this here for a while, just in case.
//    if (isJustInsertedWithOutRowId())
//    {
//        QString msg = tr("When inserted new row to the WITHOUT ROWID table, using DEFAULT value for PRIMARY KEY, "
//                         "the table has to be reloaded in order to edit the new row.");
//        return tr("This cell is not editable, because: %1").arg(msg);
//    }

    SqlQueryModel *model = getModel();
    Db* db = model->getDb();
    if (!db->isOpen())
    {
        qWarning() << "Tried to load the data for a cell that refers to the already closed database.";
        return tr("Cannot load the data for a cell that refers to the already closed database.");
    }

    // Query
    QString query;
    QHash<QString,QVariant> queryArgs;
    if (col->editionForbiddenReason.size() > 0)
    {
        static_qstring(tpl, "SELECT %1 FROM (%2) LIMIT 1 OFFSET %3");

        // The query
        QString column = wrapObjIfNeeded(!col->alias.isNull() ? col->alias : col->column);
        query = tpl.arg(column, model->getQuery(), QString::number(index().row()));
    }
    else
    {
        static_qstring(tpl, "SELECT %1 FROM %2 WHERE %3");

        // Column
        QString column = wrapObjIfNeeded(col->column);

        // Db and table
        QString source = wrapObjIfNeeded(col->table);
        if (!col->database.isNull())
            source.prepend(wrapObjIfNeeded(col->database)+".");

        // ROWID
        RowIdConditionBuilder rowIdBuilder;
        rowIdBuilder.setRowId(getRowId());
        QString rowId = rowIdBuilder.build();
        queryArgs = rowIdBuilder.getQueryArgs();

        // The query
        query = tpl.arg(column, source, rowId);
    }

    // Get the data
    SqlQueryPtr results = db->exec(query, queryArgs);
    if (results->isError())
        return results->getErrorText();

    setValue(results->getSingleCell(), false, true);
    return QString();
}

QVariant SqlQueryItem::getFullValue()
{
    if (!isLimitedValue())
        return getValue();

    QVariant originalValue = getValue();
    loadFullData();
    QVariant result = getValue();
    setValue(originalValue, true, !isUncommitted());
    return result;
}
