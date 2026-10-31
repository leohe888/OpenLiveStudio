import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"

Rectangle {
    id: root
    objectName: "sourceFiltersDialog"
    property var filters: []
    property string selectedId: ""
    readonly property int selectedIndex: filters.findIndex(f => f.id === selectedId)
    readonly property var selectedFilter: selectedIndex >= 0 ? filters[selectedIndex] : null
    implicitWidth: 680
    implicitHeight: 380
    color: uiColors.background.panel
    signal closeRequested()

    function refresh() {
        filters = videoSourceModel.sourceFilters(sourceId)
        if (!filters.some(f => f.id === selectedId))
            selectedId = filters.length ? filters[0].id : ""
    }
    function updateParameter(key, value) {
        if (!selectedFilter) return
        videoSourceModel.setFilterParameters(sourceId, selectedId,
            key === "brightness" ? value : selectedFilter.brightness,
            key === "contrast" ? value : selectedFilter.contrast,
            key === "saturation" ? value : selectedFilter.saturation)
    }
    Component.onCompleted: refresh()
    Connections {
        target: videoSourceModel
        function onSourceFiltersChanged(id) { if (id === sourceId) root.refresh() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 20
        ColumnLayout {
            Layout.preferredWidth: 250
            Layout.maximumWidth: 250
            Layout.fillHeight: true
            StudioLabel { text: qsTr("滤镜 · 从上到下处理") }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: uiColors.background.input
                border.color: uiColors.border.default
                ListView {
                    id: filterList
                    objectName: "filterList"
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: root.filters
                    ScrollBar.vertical: StudioScrollBar { }
                    delegate: StudioItemDelegate {
                        highlighted: row.modelData.id === root.selectedId
                        id: row
                        required property var modelData
                        required property int index
                        objectName: "filterRow_" + modelData.id
                        width: ListView.view.width
                        height: 38
                        text: qsTr("色彩校正 %1").arg(index + 1)
                        leftPadding: 38
                        onClicked: root.selectedId = modelData.id
                        StudioIconButton {
                            outlined: true
                            objectName: "toggleFilter_" + row.modelData.id
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 32
                            text: row.modelData.enabled ? qsTr("停用滤镜") : qsTr("启用滤镜")
                            icon.source: "qrc:/assets/icons/" + (row.modelData.enabled ? "eye" : "eye-off") + ".svg"
                            onClicked: videoSourceModel.setFilterEnabled(sourceId, row.modelData.id, !row.modelData.enabled)
                        }
                    }
                    StudioLabel {
                        anchors.centerIn: parent
                        visible: root.filters.length === 0
                        text: qsTr("暂无滤镜")
                        color: uiColors.text.secondary
                    }
                }
            }
            RowLayout {
                StudioButton {
                    ToolTip.visible: hovered
                    ToolTip.text: text
                    objectName: "addFilterButton"
                    text: qsTr("添加色彩校正")
                    icon.source: "qrc:/assets/icons/plus.svg"
                    onClicked: root.selectedId = videoSourceModel.addColorCorrection(sourceId)
                }
                Repeater {
                    model: [ { action: "remove", icon: "minus", label: qsTr("删除滤镜") },
                             { action: "up", icon: "arrow-up", label: qsTr("滤镜上移") },
                             { action: "down", icon: "arrow-down", label: qsTr("滤镜下移") } ]
                    delegate: StudioIconButton {
                        outlined: true
                        required property var modelData
                        objectName: modelData.action + "FilterButton"
                        text: modelData.label
                        icon.source: "qrc:/assets/icons/" + modelData.icon + ".svg"
                        enabled: root.selectedIndex >= 0 && (modelData.action === "remove"
                            || (modelData.action === "up" ? root.selectedIndex > 0 : root.selectedIndex < root.filters.length - 1))
                        onClicked: {
                            if (modelData.action === "remove")
                                videoSourceModel.removeFilter(sourceId, root.selectedId)
                            else videoSourceModel.moveFilter(sourceId, root.selectedId, modelData.action === "up" ? -1 : 1)
                        }
                    }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            StudioLabel { text: qsTr("色彩校正") }
            StudioLabel {
                Layout.fillWidth: true
                text: qsTr("修改立即生效，关闭后保留。")
                color: uiColors.text.secondary
            }
            Repeater {
                model: [ { key: "brightness", label: qsTr("亮度"), min: -1, max: 1, initial: 0 },
                         { key: "contrast", label: qsTr("对比度"), min: 0, max: 2, initial: 1 },
                         { key: "saturation", label: qsTr("饱和度"), min: 0, max: 2, initial: 1 } ]
                delegate: ColumnLayout {
                    id: parameter
                    required property var modelData
                    Layout.fillWidth: true
                    enabled: root.selectedFilter !== null
                    StudioLabel { text: parameter.modelData.label }
                    RowLayout {
                        StudioSlider {
                            id: slider
                            objectName: parameter.modelData.key + "Slider"
                            Layout.fillWidth: true
                            from: parameter.modelData.min
                            to: parameter.modelData.max
                            stepSize: 0.01
                            value: root.selectedFilter ? root.selectedFilter[parameter.modelData.key] : parameter.modelData.initial
                            onMoved: root.updateParameter(parameter.modelData.key, value)
                            Accessible.name: parameter.modelData.label
                        }
                        StudioSpinBox {
                            id: number
                            objectName: parameter.modelData.key + "Input"
                            Layout.preferredWidth: 140
                            from: parameter.modelData.min * 100
                            to: parameter.modelData.max * 100
                            value: Math.round((root.selectedFilter ? root.selectedFilter[parameter.modelData.key] : parameter.modelData.initial) * 100)
                            editable: true
                            Accessible.name: parameter.modelData.label
                            textFromValue: function(value, locale) { return Number(value / 100).toLocaleString(locale, 'f', 2) }
                            valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text) * 100) }
                            validator: DoubleValidator { bottom: number.from / 100; top: number.to / 100; decimals: 2; locale: number.locale.name }
                            onValueModified: root.updateParameter(parameter.modelData.key, value / 100)
                        }
                    }
                }
            }
            StudioButton {
                ToolTip.visible: hovered
                ToolTip.text: text
                objectName: "resetFilterButton"
                text: qsTr("恢复默认")
                enabled: root.selectedFilter !== null
                onClicked: videoSourceModel.setFilterParameters(sourceId, root.selectedId, 0, 1, 1)
            }
            Item { Layout.fillHeight: true }
            StudioButton {
                ToolTip.visible: hovered
                ToolTip.text: text
                objectName: "closeFiltersButton"
                Layout.alignment: Qt.AlignRight
                text: qsTr("关闭")
                onClicked: root.closeRequested()
            }
        }
    }
}
