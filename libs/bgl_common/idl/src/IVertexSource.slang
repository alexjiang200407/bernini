// Vertex bytes as the decode reads them: one typed load at a byte offset. The decode is generic
// over this so it names no buffer; the renderer's raw buffer conforms by declaration, and a
// renderer without bindless supplies whatever holds its vertices.
public interface IVertexSource
{
    T Load<T>(uint byteOffset);
};
